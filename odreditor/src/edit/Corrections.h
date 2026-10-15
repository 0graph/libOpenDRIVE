#pragma once
#include "ReferenceIndex.h"
#include <unordered_set>

namespace odreditor
{
using IdChanges = std::map<NodeId, std::string>;
using DeletedNodes = std::unordered_set<NodeId>;
DeletedNodes        CollectSubtree(const XodrDocument& document, NodeId root);
std::vector<NodeId> GetSideLanes(const XodrDocument& document, NodeId side);
void                NumberLanes(const XodrDocument& document, NodeId side, const std::vector<NodeId>& order, IdChanges& changes);
void                ApplyCorrections(XodrDocument& document, const ReferenceIndex& index, const IdChanges& changes, const DeletedNodes& deleted);
void                CheckMovedReferences(const XodrDocument& document, const ReferenceIndex& before);
} // namespace odreditor
