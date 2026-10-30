// ============================================================================
// object.cpp ― ObjectManager の実装 (object.hpp の宣言に対応)
// ============================================================================

#include "object.hpp"

#include <algorithm>

IObject* ObjectManager::find(const std::string& name) const {
    for (const auto& obj : objects_) {
        if (obj->name() == name) {
            return obj.get();
        }
    }
    return nullptr;
}

void ObjectManager::destroy(const std::string& name) {
    if (IObject* obj = find(name)) {
        obj->destroy();
    }
}

void ObjectManager::sweep() {
    objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                  [](const std::unique_ptr<IObject>& obj) { return obj->dead(); }),
                   objects_.end());
}

void ObjectManager::updateAll(float dt) {
    for (const auto& obj : objects_) {
        if (obj->active() && !obj->dead()) {
            obj->update(dt);
        }
    }
    sweep();  // 更新中に予約された破棄を、ここでまとめて実行する
}

void ObjectManager::drawAll(IRenderer& renderer) {
    for (const auto& obj : objects_) {
        if (obj->active() && !obj->dead()) {
            obj->draw(renderer);
        }
    }
}
