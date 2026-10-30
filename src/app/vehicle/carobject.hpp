#pragma once
// ============================================================================
// carobject.hpp ― プレイヤー車のオブジェクト化 (CarObject)
// 役割: 物理クラス Car を IObject としてマネージャに登録するための
//       アダプタ。"Player" という名前で Find できるようになり、
//       シーン側は「名前で車を引く」形で参照できる。物理そのものは
//       Car が担当し、毎フレームの駆動は RaceScene が行う (入力が
//       必要なため一括更新には乗せず、登録と検索のためにここに置く)。
// ============================================================================

#include "car.hpp"     // Car (物理)
#include "object.hpp"  // IObject

class CarObject : public IObject {
public:
    explicit CarObject(Car& car) : car_(car) {}

    Car& car() { return car_; }
    const Car& car() const { return car_; }

private:
    Car& car_;  // 実体は Game が保持する Car への参照
};
