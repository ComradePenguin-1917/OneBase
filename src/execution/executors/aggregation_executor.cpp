#include "onebase/execution/executors/aggregation_executor.h"
#include "onebase/common/exception.h"
#include "onebase/type/value.h"
#include <unordered_map>
#include <limits>

namespace onebase {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                          std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void AggregationExecutor::Init() {
  child_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;
  
  const auto &group_bys = plan_->GetGroupBys();
  const auto &aggregates = plan_->GetAggregates();
  const auto &agg_types = plan_->GetAggregateTypes();
  const auto &schema = child_executor_->GetOutputSchema();
  
  struct AggState {
    std::vector<Value> group_values;
    int32_t count_star = 0;
    int32_t count = 0;
    int32_t sum = 0;
    int32_t min_val = std::numeric_limits<int32_t>::max();
    int32_t max_val = std::numeric_limits<int32_t>::min();
    bool has_value = false;
    bool has_sum = false;
    bool has_min = false;
    bool has_max = false;
  };
  
  std::unordered_map<std::string, AggState> groups;
  bool has_input = false;
  
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    has_input = true;
    
    std::string group_key;
    std::vector<Value> group_val;
    for (const auto &expr : group_bys) {
      auto val = expr->Evaluate(&child_tuple, &schema);
      group_key += val.ToString() + "|";
      group_val.push_back(val);
    }
    
    auto &state = groups[group_key];
    if (state.count_star == 0) {
      state.group_values = group_val;
    }
    state.has_value = true;
    state.count_star++;
    
    for (size_t i = 0; i < aggregates.size(); i++) {
      auto val = aggregates[i]->Evaluate(&child_tuple, &schema);
      switch (agg_types[i]) {
        case AggregationType::CountStarAggregate:
          break;
        case AggregationType::CountAggregate:
          if (!val.IsNull()) {
            state.count++;
          }
          break;
        case AggregationType::SumAggregate:
          if (!val.IsNull()) {
            state.sum += val.GetAsInteger();
            state.has_sum = true;
          }
          break;
        case AggregationType::MinAggregate:
          if (!val.IsNull()) {
            if (!state.has_min || val.GetAsInteger() < state.min_val) {
              state.min_val = val.GetAsInteger();
              state.has_min = true;
            }
          }
          break;
        case AggregationType::MaxAggregate:
          if (!val.IsNull()) {
            if (!state.has_max || val.GetAsInteger() > state.max_val) {
              state.max_val = val.GetAsInteger();
              state.has_max = true;
            }
          }
          break;
      }
    }
  }
  
  if (!has_input && group_bys.empty()) {
    std::vector<Value> values;
    for (size_t i = 0; i < agg_types.size(); i++) {
      switch (agg_types[i]) {
        case AggregationType::CountStarAggregate:
        case AggregationType::CountAggregate:
          values.emplace_back(TypeId::INTEGER, 0);
          break;
        case AggregationType::SumAggregate:
        case AggregationType::MinAggregate:
        case AggregationType::MaxAggregate:
          values.emplace_back(TypeId::INTEGER, static_cast<int32_t>(0));
          break;
      }
    }
    result_tuples_.emplace_back(std::move(values));
    return;
  }
  
  for (auto &[key, state] : groups) {
    std::vector<Value> values;
    
    if (!group_bys.empty()) {
      values = state.group_values;
    }
    
    for (size_t i = 0; i < agg_types.size(); i++) {
      switch (agg_types[i]) {
        case AggregationType::CountStarAggregate:
          values.emplace_back(TypeId::INTEGER, state.count_star);
          break;
        case AggregationType::CountAggregate:
          values.emplace_back(TypeId::INTEGER, state.count);
          break;
        case AggregationType::SumAggregate:
          if (state.has_sum) {
            values.emplace_back(TypeId::INTEGER, state.sum);
          } else {
            values.emplace_back(TypeId::INTEGER, static_cast<int32_t>(0));
          }
          break;
        case AggregationType::MinAggregate:
          if (state.has_min) {
            values.emplace_back(TypeId::INTEGER, state.min_val);
          } else {
            values.emplace_back(TypeId::INTEGER, static_cast<int32_t>(0));
          }
          break;
        case AggregationType::MaxAggregate:
          if (state.has_max) {
            values.emplace_back(TypeId::INTEGER, state.max_val);
          } else {
            values.emplace_back(TypeId::INTEGER, static_cast<int32_t>(0));
          }
          break;
      }
    }
    result_tuples_.emplace_back(std::move(values));
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  return true;
}

}  // namespace onebase
