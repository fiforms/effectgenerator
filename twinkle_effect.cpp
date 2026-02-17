// twinkle_effect.cpp
// Twinkling stars effect: static stars that fade in/out randomly.

#include "effect_generator.h"
#include <random>
#include <cmath>
#include <algorithm>
#include <iostream>

struct Star {
    float x, y;
    float radius;
    float baseOpacity;
    float phase;
    float freq;
    float amp;
    int type; // 0 = small round, 1 = star-of-bethlehem
    float colorR, colorG, colorB;
    // lifecycle
    float age; // seconds
    float lifeDuration; // seconds until disappear
    float fadeInDuration;
    float holdDuration;
    float fadeOutDuration;
    float respawnTimer; // seconds until respawn when dead
    float vx;
    float vy;
    bool alive;
    bool tracked;
    bool needsRelock;
};

struct BrightSpot {
    int x, y;
    float score;
};

class TwinkleEffect : public Effect {
private:
    int width_, height_, fps_;
    int numStars_;
    float avgSpeed_; // controls pulsing frequency
    float softness_;
    float smallMaxRadius_;
    float bethlehemWidth_; // branch width
    float bethlehemCenterBoost_;
    int frameCount_;
    std::vector<Star> stars_;
    std::mt19937 rng_;
    int mode_; // 0=small,1=bethlehem,2=mixed
    float mixRatio_; // when mixed, fraction of small stars (0..1)
    float groundThreshold_ = 0.0f; // output pixels at bottom to avoid placing stars on
    bool trackBrightSpots_;
    int maxHotspots_;
    float brightThreshold_;
    float contrastThreshold_;
    float trackingRadius_;
    float nmsRadius_;
    int detectStep_;
    float hotspotOpacityBias_;
    float darkenStrength_;
    std::vector<float> luma_;
    std::vector<BrightSpot> prevHotspots_;
    bool havePrevHotspots_ = false;
    float filteredGlobalDx_ = 0.0f;
    float filteredGlobalDy_ = 0.0f;

    float clamp01(float v) const {
        return std::max(0.0f, std::min(1.0f, v));
    }

    float offscreenMargin() const {
        // Allow stars to drift slightly out of frame so edge motion looks natural.
        return std::max(24.0f, trackingRadius_ * 4.0f + nmsRadius_ * 1.5f);
    }

    void moveStarToward(Star& s, float targetX, float targetY) {
        float dx = targetX - s.x;
        float dy = targetY - s.y;
        float dist2 = dx * dx + dy * dy;
        if (dist2 <= 0.0001f) {
            s.vx *= 0.75f;
            s.vy *= 0.75f;
            return;
        }
        float dist = std::sqrt(dist2);
        // Deadzone removes 1px-scale detector jitter.
        const float deadzone = 1.15f;
        if (dist < deadzone) {
            s.vx *= 0.70f;
            s.vy *= 0.70f;
            s.x += s.vx;
            s.y += s.vy;
            return;
        }
        // Keep motion smooth via damped velocity instead of hard snapping.
        float maxStep = std::max(0.5f, trackingRadius_ * 0.45f);
        float responsiveness = 0.23f;
        s.vx = s.vx * 0.72f + dx * responsiveness;
        s.vy = s.vy * 0.72f + dy * responsiveness;
        float speed = std::sqrt(s.vx * s.vx + s.vy * s.vy);
        if (speed > maxStep && speed > 0.0001f) {
            float k = maxStep / speed;
            s.vx *= k;
            s.vy *= k;
        }
        s.x += s.vx;
        s.y += s.vy;
    }

    bool estimateGlobalShiftRobust(const std::vector<BrightSpot>& prevSpots,
                                   const std::vector<BrightSpot>& curSpots,
                                   float& outDx, float& outDy) const {
        outDx = 0.0f;
        outDy = 0.0f;
        if (prevSpots.empty() || curSpots.empty()) return false;

        int kPrev = std::min((int)prevSpots.size(), 120);
        int kCur = std::min((int)curSpots.size(), 240);
        float maxMatchDist = std::max(6.0f, trackingRadius_ * 3.5f);
        float maxMatchDist2 = maxMatchDist * maxMatchDist;

        std::vector<float> dxs;
        std::vector<float> dys;
        dxs.reserve((size_t)kPrev);
        dys.reserve((size_t)kPrev);

        for (int i = 0; i < kPrev; ++i) {
            const auto& p = prevSpots[i];
            int best = -1;
            float bestDist2 = maxMatchDist2;
            for (int j = 0; j < kCur; ++j) {
                float dx = (float)curSpots[j].x - p.x;
                float dy = (float)curSpots[j].y - p.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < bestDist2) {
                    bestDist2 = d2;
                    best = j;
                }
            }
            if (best >= 0) {
                dxs.push_back((float)curSpots[best].x - p.x);
                dys.push_back((float)curSpots[best].y - p.y);
            }
        }

        if (dxs.size() < 8 || dys.size() < 8) return false;

        auto midX = dxs.begin() + (dxs.size() / 2);
        auto midY = dys.begin() + (dys.size() / 2);
        std::nth_element(dxs.begin(), midX, dxs.end());
        std::nth_element(dys.begin(), midY, dys.end());
        outDx = *midX;
        outDy = *midY;
        return true;
    }

    void detectBrightHotspots(const std::vector<uint8_t>& frame, std::vector<BrightSpot>& hotspots) {
        hotspots.clear();
        if (width_ < 5 || height_ < 5) return;

        luma_.assign(width_ * height_, 0.0f);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                int idx = (y * width_ + x) * 3;
                float r = frame[idx + 0];
                float g = frame[idx + 1];
                float b = frame[idx + 2];
                luma_[y * width_ + x] = 0.299f * r + 0.587f * g + 0.114f * b;
            }
        }

        std::vector<BrightSpot> candidates;
        candidates.reserve((size_t)width_ * height_ / 16);
        int maxYAllowed = std::min(height_ - 3, std::max(2, (int)std::floor((float)height_ - groundThreshold_) - 1));
        int step = std::max(1, detectStep_);
        for (int y = 2; y <= maxYAllowed; y += step) {
            for (int x = 2; x < width_ - 2; x += step) {
                float centerSum = 0.0f;
                float ringSum = 0.0f;
                for (int oy = -2; oy <= 2; ++oy) {
                    for (int ox = -2; ox <= 2; ++ox) {
                        float lum = luma_[(y + oy) * width_ + (x + ox)];
                        if (std::abs(ox) <= 1 && std::abs(oy) <= 1) centerSum += lum;
                        else ringSum += lum;
                    }
                }

                float centerAvg = centerSum / 9.0f;
                float ringAvg = ringSum / 16.0f;
                float contrast = centerAvg - ringAvg;
                if (centerAvg < brightThreshold_ || contrast < contrastThreshold_) continue;

                float c = luma_[y * width_ + x];
                if (c < luma_[(y - 1) * width_ + (x - 1)] || c < luma_[(y - 1) * width_ + x] || c < luma_[(y - 1) * width_ + (x + 1)] ||
                    c < luma_[y * width_ + (x - 1)]     || c < luma_[y * width_ + (x + 1)]     ||
                    c < luma_[(y + 1) * width_ + (x - 1)] || c < luma_[(y + 1) * width_ + x] || c < luma_[(y + 1) * width_ + (x + 1)]) {
                    continue;
                }

                float score = contrast * (centerAvg / 255.0f);
                candidates.push_back({x, y, score});
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const BrightSpot& a, const BrightSpot& b) { return a.score > b.score; });

        float nmsRad2 = nmsRadius_ * nmsRadius_;
        for (const auto& cand : candidates) {
            if ((int)hotspots.size() >= maxHotspots_) break;
            bool tooClose = false;
            for (const auto& picked : hotspots) {
                float dx = (float)cand.x - picked.x;
                float dy = (float)cand.y - picked.y;
                if (dx * dx + dy * dy < nmsRad2) {
                    tooClose = true;
                    break;
                }
            }
            if (!tooClose) hotspots.push_back(cand);
        }
    }

    void trackStarsToHotspots(const std::vector<BrightSpot>& hotspots) {
        if (hotspots.empty()) {
            for (auto& s : stars_) s.tracked = false;
            havePrevHotspots_ = false;
            prevHotspots_.clear();
            filteredGlobalDx_ = 0.0f;
            filteredGlobalDy_ = 0.0f;
            return;
        }

        std::vector<bool> used(hotspots.size(), false);
        float maxDist2 = trackingRadius_ * trackingRadius_;
        float reacquireDist2 = (trackingRadius_ * 3.0f) * (trackingRadius_ * 3.0f);
        float maxScore = hotspots.front().score;
        if (maxScore <= 0.0001f) maxScore = 1.0f;

        // Estimate global drift between consecutive hotspot fields and pre-shift stars.
        // Use robust median flow so dense star fields do not jitter.
        if (havePrevHotspots_ && !prevHotspots_.empty()) {
            float dx = 0.0f, dy = 0.0f;
            if (estimateGlobalShiftRobust(prevHotspots_, hotspots, dx, dy)) {
                // Low-pass the frame-to-frame drift estimate to reduce jitter.
                filteredGlobalDx_ = filteredGlobalDx_ * 0.82f + dx * 0.18f;
                filteredGlobalDy_ = filteredGlobalDy_ * 0.82f + dy * 0.18f;
                dx = filteredGlobalDx_;
                dy = filteredGlobalDy_;
                float maxShift = trackingRadius_ * 1.4f;
                float shiftLen = std::sqrt(dx * dx + dy * dy);
                if (shiftLen > maxShift && shiftLen > 0.0001f) {
                    float s = maxShift / shiftLen;
                    dx *= s;
                    dy *= s;
                }
                float margin = offscreenMargin();
                for (auto& st : stars_) {
                    if (!st.alive) continue;
                    st.x = std::clamp(st.x + dx, -margin, (float)(width_ - 1) + margin);
                    st.y = std::clamp(st.y + dy, -margin, (float)(height_ - 1) + margin);
                }
            }
        } else {
            filteredGlobalDx_ = 0.0f;
            filteredGlobalDy_ = 0.0f;
        }

        if (frameCount_ == 0) {
            // Initial lock-on so stars start on bright spots, then remain local.
            for (size_t si = 0; si < stars_.size(); ++si) {
                auto& s = stars_[si];
                if (!s.alive) continue;
                const auto& hs = hotspots[si % hotspots.size()];
                s.x = (float)hs.x;
                s.y = (float)hs.y;
                float targetOpacity = 0.2f + 0.8f * clamp01(hs.score / maxScore);
                s.baseOpacity = std::clamp(s.baseOpacity * (1.0f - hotspotOpacityBias_) + targetOpacity * hotspotOpacityBias_, 0.05f, 1.0f);
                s.tracked = true;
                s.needsRelock = false;
            }
            prevHotspots_ = hotspots;
            havePrevHotspots_ = true;
            return;
        }

        bool denseField = ((int)hotspots.size() >= std::max(24, numStars_ / 2));
        for (auto& s : stars_) {
            if (!s.alive) continue;
            if (s.needsRelock) {
                int pick = -1;
                float bestScore = -1e30f;
                for (size_t h = 0; h < hotspots.size(); ++h) {
                    if (used[h]) continue;
                    if (hotspots[h].score > bestScore) {
                        bestScore = hotspots[h].score;
                        pick = (int)h;
                    }
                }
                if (pick < 0) {
                    pick = 0;
                    float bestDist2 = 1e30f;
                    for (size_t h = 0; h < hotspots.size(); ++h) {
                        float dx = (float)hotspots[h].x - s.x;
                        float dy = (float)hotspots[h].y - s.y;
                        float dist2 = dx * dx + dy * dy;
                        if (dist2 < bestDist2) {
                            bestDist2 = dist2;
                            pick = (int)h;
                        }
                    }
                }
                used[pick] = true;
                s.x = (float)hotspots[pick].x;
                s.y = (float)hotspots[pick].y;
                s.vx = 0.0f;
                s.vy = 0.0f;
                float targetOpacity = 0.2f + 0.8f * clamp01(hotspots[pick].score / maxScore);
                s.baseOpacity = std::clamp(s.baseOpacity * (1.0f - hotspotOpacityBias_) + targetOpacity * hotspotOpacityBias_, 0.05f, 1.0f);
                s.tracked = true;
                s.needsRelock = false;
                continue;
            }
            int best = -1;
            float bestDist2 = denseField ? (maxDist2 * 0.45f) : maxDist2;
            for (size_t h = 0; h < hotspots.size(); ++h) {
                if (used[h]) continue;
                float dx = (float)hotspots[h].x - s.x;
                float dy = (float)hotspots[h].y - s.y;
                float dist2 = dx * dx + dy * dy;
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    best = (int)h;
                }
            }
            if (best >= 0) {
                used[best] = true;
                moveStarToward(s, (float)hotspots[best].x, (float)hotspots[best].y);
                float targetOpacity = 0.2f + 0.8f * clamp01(hotspots[best].score / maxScore);
                s.baseOpacity = std::clamp(s.baseOpacity * (1.0f - hotspotOpacityBias_) + targetOpacity * hotspotOpacityBias_, 0.05f, 1.0f);
                s.tracked = true;
            } else {
                s.tracked = false;
            }
        }

        for (auto& s : stars_) {
            if (!s.alive || s.tracked) continue;
            // Re-acquire with wider, capped radius (still local, avoids screen jumps).
            // For dense fields, allow gentle reuse because exact identity is ambiguous.
            int fallback = -1;
            float bestCost = 1e30f;
            for (size_t h = 0; h < hotspots.size(); ++h) {
                float dx = (float)hotspots[h].x - s.x;
                float dy = (float)hotspots[h].y - s.y;
                float dist2 = dx * dx + dy * dy;
                if (dist2 > reacquireDist2) continue;
                float reusePenalty = used[h] ? (denseField ? (nmsRadius_ * nmsRadius_ * 0.12f)
                                                           : (nmsRadius_ * nmsRadius_ * 0.35f))
                                             : 0.0f;
                float cost = dist2 + reusePenalty;
                if (cost < bestCost) {
                    bestCost = cost;
                    fallback = (int)h;
                }
            }
            if (fallback < 0) continue;
            used[fallback] = true;
            moveStarToward(s, (float)hotspots[fallback].x, (float)hotspots[fallback].y);
            float targetOpacity = 0.2f + 0.8f * clamp01(hotspots[fallback].score / maxScore);
            s.baseOpacity = std::clamp(s.baseOpacity * (1.0f - hotspotOpacityBias_) + targetOpacity * hotspotOpacityBias_, 0.05f, 1.0f);
            s.tracked = true;
        }

        prevHotspots_ = hotspots;
        havePrevHotspots_ = true;
    }

    void darkenDisk(std::vector<uint8_t>& frame, float cx, float cy, float radius, float amount, float fadeMultiplier) {
        if (amount <= 0.0001f || radius <= 0.1f) return;
        float strength = std::clamp(amount * darkenStrength_ * fadeMultiplier, 0.0f, 1.0f);
        if (strength <= 0.0001f) return;
        int rI = (int)std::ceil(radius * 2.2f + 1.0f);
        int minX = std::max(0, (int)std::floor(cx - rI));
        int maxX = std::min(width_ - 1, (int)std::ceil(cx + rI));
        int minY = std::max(0, (int)std::floor(cy - rI));
        int maxY = std::min(height_ - 1, (int)std::ceil(cy + rI));
        float rr = std::max(0.0001f, radius);
        float invSigma2 = 1.0f / (rr * rr * 0.9f);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float dx = (x + 0.5f) - cx;
                float dy = (y + 0.5f) - cy;
                float dist2 = dx * dx + dy * dy;
                float a = std::exp(-dist2 * invSigma2) * strength;
                if (a <= 0.001f) continue;
                float factor = std::clamp(1.0f - a, 0.0f, 1.0f);
                int idx = (y * width_ + x) * 3;
                frame[idx + 0] = (uint8_t)std::round(frame[idx + 0] * factor);
                frame[idx + 1] = (uint8_t)std::round(frame[idx + 1] * factor);
                frame[idx + 2] = (uint8_t)std::round(frame[idx + 2] * factor);
            }
        }
    }

    void drawEllipse(std::vector<uint8_t>& frame, int cx, int cy, float rx, float ry, float opacity, float fadeMultiplier, float colR, float colG, float colB) {
        float effectiveRx = rx + softness_;
        float effectiveRy = ry + softness_;

        int minX = std::max(0, (int)(cx - effectiveRx - 2));
        int maxX = std::min(width_ - 1, (int)(cx + effectiveRx + 2));
        int minY = std::max(0, (int)(cy - effectiveRy - 2));
        int maxY = std::min(height_ - 1, (int)(cy + effectiveRy + 2));

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float dx = (x + 0.5f) - cx;
                float dy = (y + 0.5f) - cy;

                float nx = dx / std::max(0.0001f, rx);
                float ny = dy / std::max(0.0001f, ry);
                float ellDist = std::sqrt(nx * nx + ny * ny);

                float alpha = 0.0f;

                if (ellDist < 1.0f + (softness_ / std::max(rx, ry))) {
                    if (ellDist <= 1.0f) {
                        float t = ellDist;
                        alpha = 1.0f - t * 0.12f;
                    } else {
                        float t = (ellDist - 1.0f) * (std::max(rx, ry) / softness_);
                        t = t * t * (3.0f - 2.0f * t);
                        alpha = 0.9f * (1.0f - t);
                    }
                }

                alpha = std::clamp(alpha * opacity * fadeMultiplier, 0.0f, 1.0f);

                if (alpha > 0.005f) {
                    int idx = (y * width_ + x) * 3;
                    float addR = alpha * colR;
                    float addG = alpha * colG;
                    float addB = alpha * colB;
                    float currentR = frame[idx + 0] / 255.0f;
                    float currentG = frame[idx + 1] / 255.0f;
                    float currentB = frame[idx + 2] / 255.0f;
                    float resR = std::min(1.0f, currentR + addR);
                    float resG = std::min(1.0f, currentG + addG);
                    float resB = std::min(1.0f, currentB + addB);
                    frame[idx + 0] = (uint8_t)(255 * resR);
                    frame[idx + 1] = (uint8_t)(255 * resG);
                    frame[idx + 2] = (uint8_t)(255 * resB);
                }
            }
        }
    }

    // Draw a star-of-bethlehem centered at (cx,cy) with orientation 0 (vertical/horizontal cross)
    // Uses an asymptotic decay along branches and gaussian falloff perpendicular to branch.
    void drawStarBethlehem(std::vector<uint8_t>& frame, int cx, int cy, float intensity, float fadeMultiplier, float colR, float colG, float colB) {
        // Precompute bounds depending on branch lengths
        // We'll limit drawing to a local box for performance; bigger box for longer tails
        int radius = (int)std::ceil(std::min((float)std::max(width_, height_), 180.0f));
        int minX = std::max(0, cx - radius);
        int maxX = std::min(width_ - 1, cx + radius);
        int minY = std::max(0, cy - radius);
        int maxY = std::min(height_ - 1, cy + radius * 2);

        // branch directions: up, right, left, down (down is longer)
        const float dirs[4][2] = {{0,-1},{1,0},{-1,0},{0,1}};
        // relative decay per branch; down (index 3) has much smaller decay => much longer tail
        // smaller values make the asymptote 1/(1 + k*t) decay slower -> longer tail
        const float decayBase[4] = {0.08f, 0.08f, 0.08f, 0.02f};
        const float widthBase = bethlehemWidth_;

        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                float rx = (x + 0.5f) - cx;
                float ry = (y + 0.5f) - cy;

                float acc = 0.0f;

                // center glow
                float dist2 = (rx*rx + ry*ry);
                float center = std::exp(-dist2 / (2.0f * 1.5f * 1.5f));
                acc += center * 0.9f;

                for (int b = 0; b < 4; b++) {
                    float dx = dirs[b][0];
                    float dy = dirs[b][1];
                    // projection along branch
                    float t = rx * dx + ry * dy; // positive in branch direction
                    if (t <= 0.0f) continue;
                    // perpendicular distance
                    float px = rx - t * dx;
                    float py = ry - t * dy;
                    float perp2 = px*px + py*py;

                    // asymptotic decay along branch: ~ 1/(1 + decay * t)
                    float branchDecay = decayBase[b];
                    float along = 5.0f / (1.0f + branchDecay * t * t);
                    // gaussian perpendicular falloff
                    float perp = std::exp(-perp2 / (2.0f * widthBase * widthBase));
                    acc += along * perp * 1.2f;
                }

                // scale by intensity and fade
                float alpha = std::clamp(acc * intensity * fadeMultiplier, 0.0f, 1.0f);
                if (alpha > 0.001f) {
                    int idx = (y * width_ + x) * 3;
                    float addR = alpha * colR;
                    float addG = alpha * colG;
                    float addB = alpha * colB;
                    float currentR = frame[idx + 0] / 255.0f;
                    float currentG = frame[idx + 1] / 255.0f;
                    float currentB = frame[idx + 2] / 255.0f;
                    float resR = std::min(1.0f, currentR + addR);
                    float resG = std::min(1.0f, currentG + addG);
                    float resB = std::min(1.0f, currentB + addB);
                    frame[idx + 0] = (uint8_t)(255 * resR);
                    frame[idx + 1] = (uint8_t)(255 * resG);
                    frame[idx + 2] = (uint8_t)(255 * resB);
                }
            }
        }
    }

public:
    TwinkleEffect()
        : numStars_(120), avgSpeed_(0.45f), softness_(1.5f), smallMaxRadius_(2.5f), bethlehemWidth_(2.0f), bethlehemCenterBoost_(0.5f), frameCount_(0), rng_(std::random_device{}()), mode_(2), mixRatio_(0.95f), groundThreshold_(0.0f),
          trackBrightSpots_(true), maxHotspots_(200), brightThreshold_(220.0f), contrastThreshold_(30.0f), trackingRadius_(14.0f), nmsRadius_(10.0f), detectStep_(1), hotspotOpacityBias_(0.55f), darkenStrength_(0.75f) {}

    std::string getName() const override { return "twinkle"; }
    std::string getDescription() const override { return "Twinkling stars with bright-spot tracking enabled by default for video backgrounds"; }

    std::vector<Effect::EffectOption> getOptions() const override {
        using Opt = Effect::EffectOption;
        std::vector<Opt> opts;
        opts.push_back({"--stars", "int", 1, 100000, true, "Number of stars", "120"});
        opts.push_back({"--twinkle-speed", "float", 0.0, 10000.0, true, "Average twinkle speed", "0.45"});
        opts.push_back({"--softness", "float", 0.0, 10000.0, true, "Edge softness/blur", "1.5"});
        opts.push_back({"--type", "string", 0, 0, false, "Star type: small, bethlehem, or mixed", "mixed", false,
                        {"small", "bethlehem", "mixed"}});
        opts.push_back({"--mix-ratio", "float", 0.0, 1.0, true, "When mixed, fraction of small stars (0..1)", "0.95"});
        opts.push_back({"--ground-threshold", "float", 0.0, 10000000.0, true, "Ground band (in output pixels from bottom) where stars are not placed", "0.0"});
        opts.push_back({"--no-track-bright-spots", "boolean", 0, 1, false, "Disable bright-point tracking", "false"});
        opts.push_back({"--hotspots", "int", 1, 100000, true, "Maximum bright spots to detect per frame", "200", true});
        opts.push_back({"--bright-threshold", "float", 0.0, 255.0, true, "Minimum center luma for bright-spot detection", "220", true});
        opts.push_back({"--contrast-threshold", "float", 0.0, 255.0, true, "Required center-vs-surround contrast for spot detection", "30", true});
        opts.push_back({"--track-radius", "float", 0.0, 10000.0, true, "Max tracking distance to keep a star on the same spot", "14", true});
        opts.push_back({"--hotspot-nms-radius", "float", 0.0, 10000.0, true, "Minimum separation between detected bright spots", "10", true});
        opts.push_back({"--detect-step", "int", 1, 8, true, "Detector stride in pixels (higher = faster, less precise)", "1", true});
        opts.push_back({"--hotspot-opacity-bias", "float", 0.0, 1.0, true, "How strongly star opacity follows hotspot strength", "0.55", true});
        return opts;
    }

    bool parseArgs(int argc, char** argv, int& i) override {
        std::string arg = argv[i];
        if (arg == "--stars" && i + 1 < argc) {
            numStars_ = std::atoi(argv[++i]);
            return true;
        } else if (arg == "--twinkle-speed" && i + 1 < argc) {
            avgSpeed_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--softness" && i + 1 < argc) {
            softness_ = std::atof(argv[++i]);
            return true;
        } else if (arg == "--type" && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "small") mode_ = 0;
            else if (v == "bethlehem") mode_ = 1;
            else mode_ = 2;
            return true;
        } else if (arg == "--mix-ratio" && i + 1 < argc) {
            mixRatio_ = std::atof(argv[++i]);
            if (mixRatio_ < 0.0f) mixRatio_ = 0.0f;
            if (mixRatio_ > 1.0f) mixRatio_ = 1.0f;
            return true;
        } else if (arg == "--ground-threshold" && i + 1 < argc) {
            groundThreshold_ = std::atof(argv[++i]);
            if (groundThreshold_ < 0.0f) groundThreshold_ = 0.0f;
            return true;
        } else if (arg == "--no-track-bright-spots") {
            trackBrightSpots_ = false;
            return true;
        } else if (arg == "--hotspots" && i + 1 < argc) {
            maxHotspots_ = std::max(1, std::atoi(argv[++i]));
            return true;
        } else if (arg == "--bright-threshold" && i + 1 < argc) {
            brightThreshold_ = std::clamp((float)std::atof(argv[++i]), 0.0f, 255.0f);
            return true;
        } else if (arg == "--contrast-threshold" && i + 1 < argc) {
            contrastThreshold_ = std::clamp((float)std::atof(argv[++i]), 0.0f, 255.0f);
            return true;
        } else if (arg == "--track-radius" && i + 1 < argc) {
            trackingRadius_ = std::max(0.0f, (float)std::atof(argv[++i]));
            return true;
        } else if (arg == "--hotspot-nms-radius" && i + 1 < argc) {
            nmsRadius_ = std::max(0.0f, (float)std::atof(argv[++i]));
            return true;
        } else if (arg == "--detect-step" && i + 1 < argc) {
            detectStep_ = std::max(1, std::min(8, std::atoi(argv[++i])));
            return true;
        } else if (arg == "--hotspot-opacity-bias" && i + 1 < argc) {
            hotspotOpacityBias_ = std::clamp((float)std::atof(argv[++i]), 0.0f, 1.0f);
            return true;
        }
        return false;
    }

    bool initialize(int width, int height, int fps) override {
        width_ = width; height_ = height; fps_ = fps;
        float spawnMaxY = std::max(0.0f, (float)height_ - groundThreshold_);

        std::uniform_real_distribution<float> distX(0.0f, (float)width_);
        std::uniform_real_distribution<float> distY(0.0f, spawnMaxY);
        std::uniform_real_distribution<float> distRadius(0.6f, smallMaxRadius_);
        std::uniform_real_distribution<float> distOpacity(0.2f, 1.0f);
        std::uniform_real_distribution<float> distPhase(0.0f, 2.0f * 3.14159265f);
        // slower twinkle variation + signed delta (lighten and darken)
        std::uniform_real_distribution<float> distFreq(0.08f, 0.22f);
        std::uniform_real_distribution<float> distAmp(0.25f, 0.65f);
        std::uniform_real_distribution<float> distColor(0.9f, 1.0f);
        std::uniform_real_distribution<float> distFadeIn(0.7f, 1.4f);
        std::uniform_real_distribution<float> distHold(2.0f, 3.3f);
        std::uniform_real_distribution<float> distFadeOut(1.0f, 2.1f);

        stars_.clear();
        stars_.reserve(numStars_);
        for (int i = 0; i < numStars_; ++i) {
            Star s;
            s.x = distX(rng_);
            s.y = distY(rng_);
            s.radius = distRadius(rng_);
            s.baseOpacity = distOpacity(rng_);
            s.phase = distPhase(rng_);
            s.freq = distFreq(rng_) * avgSpeed_;
            s.amp = distAmp(rng_);
            s.colorR = distColor(rng_);
            s.colorG = distColor(rng_);
            s.colorB = distColor(rng_);

            if (mode_ == 0) s.type = 0;
            else if (mode_ == 1) s.type = 1;
            else {
                // mixed mode: small with probability mixRatio_
                std::uniform_real_distribution<float> coin(0.0f, 1.0f);
                s.type = (coin(rng_) < mixRatio_) ? 0 : 1;
            }
            // lifecycle defaults
            s.age = 0.0f;
            s.fadeInDuration = distFadeIn(rng_);
            s.holdDuration = distHold(rng_);
            s.fadeOutDuration = distFadeOut(rng_);
            s.lifeDuration = s.fadeInDuration + s.holdDuration + s.fadeOutDuration;
            s.respawnTimer = 0.0f;
            s.vx = 0.0f;
            s.vy = 0.0f;
            s.alive = true;
            s.tracked = false;
            s.needsRelock = true;
            stars_.push_back(s);
        }

        frameCount_ = 0;
        return true;
    }

    void renderFrame(std::vector<uint8_t>& frame, bool hasBackground, float fadeMultiplier) override {
        const float TWO_PI = 6.28318530718f;
        float time = (fps_ > 0) ? (frameCount_ / (float)fps_) : 0.0f;
        if (trackBrightSpots_ && hasBackground) {
            std::vector<BrightSpot> hotspots;
            detectBrightHotspots(frame, hotspots);
            trackStarsToHotspots(hotspots);
        }

        // Pass 1: darken underlying background first.
        for (const auto& s : stars_) {
            if (!s.alive) continue;

            float t = time * s.freq * TWO_PI + s.phase;
            float pulse = std::sin(t);

            float env = 1.0f;
            if (s.age < s.fadeInDuration) {
                env = std::clamp(s.age / std::max(0.001f, s.fadeInDuration), 0.0f, 1.0f);
            } else if (s.age < s.fadeInDuration + s.holdDuration) {
                env = 1.0f;
            } else {
                float outAge = s.age - (s.fadeInDuration + s.holdDuration);
                env = std::clamp(1.0f - (outAge / std::max(0.001f, s.fadeOutDuration)), 0.0f, 1.0f);
            }
            float glow = s.baseOpacity * env;
            float signedDelta = glow * s.amp * pulse;
            float darken = std::max(0.0f, -signedDelta);
            if (darken <= 0.0001f) continue;

            if (s.type == 0) {
                float rx = std::max(0.6f, s.radius);
                float darkRadius = std::max(rx * 2.0f, s.tracked ? nmsRadius_ * 0.55f : rx * 1.4f);
                darkenDisk(frame, s.x, s.y, darkRadius, darken, fadeMultiplier);
            } else {
                float darkRadius = std::max(4.0f, s.tracked ? nmsRadius_ * 0.75f : 5.0f);
                darkenDisk(frame, s.x, s.y, darkRadius, darken, fadeMultiplier);
            }
        }

        // Pass 2: overlay star glow and brightening above the darkened background.
        for (const auto& s : stars_) {
            // skip dead stars (they will be handled in update/respawn)
            if (!s.alive) continue;

            // twinkle via sinusoidal + random amplitude
            float t = time * s.freq * TWO_PI + s.phase;
            float pulse = std::sin(t);

            // Envelope: fade in -> hold (>=2s) -> fade out.
            float env = 1.0f;
            if (s.age < s.fadeInDuration) {
                env = std::clamp(s.age / std::max(0.001f, s.fadeInDuration), 0.0f, 1.0f);
            } else if (s.age < s.fadeInDuration + s.holdDuration) {
                env = 1.0f;
            } else {
                float outAge = s.age - (s.fadeInDuration + s.holdDuration);
                env = std::clamp(1.0f - (outAge / std::max(0.001f, s.fadeOutDuration)), 0.0f, 1.0f);
            }
            float glow = s.baseOpacity * env;
            float signedDelta = glow * s.amp * pulse;
            float brighten = std::max(0.0f, signedDelta);

            if (s.type == 0) {
                // small round star
                float rx = std::max(0.6f, s.radius);
                float ry = rx;
                drawEllipse(frame, (int)s.x, (int)s.y, rx, ry, glow, fadeMultiplier, s.colorR, s.colorG, s.colorB);
                if (brighten > 0.0001f) {
                    drawEllipse(frame, (int)s.x, (int)s.y, rx, ry, brighten * 0.85f, fadeMultiplier, s.colorR, s.colorG, s.colorB);
                }
            } else {
                // star-of-bethlehem
                drawStarBethlehem(frame, (int)s.x, (int)s.y, glow * bethlehemCenterBoost_, fadeMultiplier, s.colorR, s.colorG, s.colorB);
                if (brighten > 0.0001f) {
                    drawStarBethlehem(frame, (int)s.x, (int)s.y, brighten * bethlehemCenterBoost_ * 0.9f, fadeMultiplier, s.colorR, s.colorG, s.colorB);
                }
            }
        }
    }

    void update() override {
        float dt = (fps_ > 0) ? (1.0f / (float)fps_) : 0.0333f;
        frameCount_++;
        float spawnMaxY = std::max(0.0f, (float)height_ - groundThreshold_);

        std::uniform_real_distribution<float> distX(0.0f, (float)width_);
        std::uniform_real_distribution<float> distY(0.0f, spawnMaxY);
        std::uniform_real_distribution<float> distRadius(0.6f, smallMaxRadius_);
        std::uniform_real_distribution<float> distOpacity(0.2f, 1.0f);
        std::uniform_real_distribution<float> distPhase(0.0f, 2.0f * 3.14159265f);
        std::uniform_real_distribution<float> distFreq(0.08f, 0.22f);
        std::uniform_real_distribution<float> distAmp(0.25f, 0.65f);
        std::uniform_real_distribution<float> distColor(0.9f, 1.0f);
        std::uniform_real_distribution<float> distFadeIn(0.7f, 1.4f);
        std::uniform_real_distribution<float> distHold(2.0f, 3.3f);
        std::uniform_real_distribution<float> distFadeOut(1.0f, 2.1f);
        std::uniform_real_distribution<float> distRespawn(0.8f, 2.2f);

        for (auto& s : stars_) {
            if (s.alive) {
                s.age += dt;
                if (s.age >= s.lifeDuration) {
                    // star disappears and starts respawn timer
                    s.alive = false;
                    s.respawnTimer = distRespawn(rng_);
                }
            } else {
                s.respawnTimer -= dt;
                if (s.respawnTimer <= 0.0f) {
                    // respawn star at new location with new parameters
                    s.x = distX(rng_);
                    s.y = distY(rng_);
                    s.radius = distRadius(rng_);
                    s.baseOpacity = distOpacity(rng_);
                    s.phase = distPhase(rng_);
                    s.freq = distFreq(rng_) * avgSpeed_;
                    s.amp = distAmp(rng_);
                    s.colorR = distColor(rng_);
                    s.colorG = distColor(rng_);
                    s.colorB = distColor(rng_);
                    s.age = 0.0f;
                    s.fadeInDuration = distFadeIn(rng_);
                    s.holdDuration = distHold(rng_);
                    s.fadeOutDuration = distFadeOut(rng_);
                    s.lifeDuration = s.fadeInDuration + s.holdDuration + s.fadeOutDuration;
                    s.respawnTimer = 0.0f;
                    s.vx = 0.0f;
                    s.vy = 0.0f;
                    s.alive = true;
                    s.tracked = false;
                    s.needsRelock = true;
                }
            }
        }
    }
};

// Register the effect
REGISTER_EFFECT(TwinkleEffect, "twinkle", "Twinkling stars with bright-spot tracking enabled by default for background video")
