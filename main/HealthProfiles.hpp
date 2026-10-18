#pragma once
#include "ProfileProbe.hpp"
#include "db/ProxyEntity.hpp"
namespace GreenRhythm {
HealthCandidate healthCandidate(const std::shared_ptr<NekoGui::ProxyEntity> &entity, bool hasFrontProxy = false);
HealthSubscription healthSubscription(const QByteArray &body, const HealthCandidate &current);
} // namespace GreenRhythm
