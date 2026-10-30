#pragma once

#include "meshbuild.hpp"

#include <string>
#include <vector>

struct TrackPose {
    bool found = false;
    float s = 0;
    float lateral = 0;  // 右が正
    float centerY = 0;
    float cross = 0;  // 右方向 1m あたりの高さ
    float yaw = 0;
    float rightX = 1;
    float rightZ = 0;
    float xzDist = 0;
    int corner = 0;
    int nameId = -1;
};

struct TrackSample {
    float x = 0;
    float z = 0;
    float y = 0;
    float cross = 0;
    float s = 0;
    int corner = 0;
    int nameId = -1;
    bool bridge = false;
};

// 鈴鹿サーキット。中心線は OSM、高さは国土地理院の標高。
struct Track {
    float originLat = 0;
    float originLon = 0;
    float halfWidth = 6.5f;
    float curbWidth = 0.8f;
    float runoff = 8.0f;
    float length = 0;
    float spawnX = 0;
    float spawnY = 0;
    float spawnZ = 0;
    float spawnYaw = 0;
    std::vector<TrackSample> samples;
    std::vector<std::string> names;
    std::vector<CpuMesh> visualChunks;  // 路面メッシュ（区間分割、描画カリング用）
    std::vector<Vec3> visualCenters;

    // コース定義テキストから読み込む。ファイル読み込みは呼び出し側
    // （platform::readTextFile）の担当で、この層は形式の解釈だけを行う。
    bool loadFromText(const std::string& text);
    // preferS が 0 以上のときは、その周回位置に近い路面を優先する。立体交差で上下を取り違えない。
    TrackPose query(float x, float z, float yHint, float preferS = -1.0f) const;
    // 平面距離だけで最寄り区間を引く版。地形の高さ決め専用: 高さで選ぶと
    // 立体交差の上下を取り違え、地形が路面を覆ったり谷になったりする。
    // 車の物理は従来の query (高さ優先) を使うこと。
    TrackPose queryPlanar(float x, float z) const;
    // queryPlanar の窓版: centerIdx の前後だけを評価する (第2の区間用)。
    TrackPose queryPlanarNear(float x, float z, int centerIdx) const;
    float wallLateral() const { return halfWidth + curbWidth + runoff; }
    const char* nameOf(int nameId) const;
};
