/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/runtime/export_resolver.h>

#include <rex/assert.h>
#include <rex/math.h>
#include <rex/string.h>

namespace rex::runtime {

ExportResolver::Table::Table(const std::string_view module_name,
                             const std::vector<Export*>* exports_by_ordinal)
    : exports_by_ordinal_(exports_by_ordinal) {
  module_name_ = rex::string::utf8_find_base_name_from_guest_path(module_name);

  exports_by_name_.reserve(exports_by_ordinal_->size());
  for (size_t i = 0; i < exports_by_ordinal_->size(); ++i) {
    auto export_entry = exports_by_ordinal_->at(i);
    if (export_entry) {
      exports_by_name_.push_back(export_entry);
    }
  }
  std::sort(
      exports_by_name_.begin(), exports_by_name_.end(),
      [](Export* a, Export* b) { return std::strcmp(a->name, b->name) < 0; });
}

ExportResolver::ExportResolver() = default;

ExportResolver::~ExportResolver() = default;

void ExportResolver::RegisterTable(
    const std::string_view module_name,
    const std::vector<rex::runtime::Export*>* exports) {
  tables_.emplace_back(module_name, exports);

  all_exports_by_name_.reserve(all_exports_by_name_.size() + exports->size());
  for (size_t i = 0; i < exports->size(); ++i) {
    auto export_entry = exports->at(i);
    if (export_entry) {
      all_exports_by_name_.push_back(export_entry);
    }
  }
  std::sort(
      all_exports_by_name_.begin(), all_exports_by_name_.end(),
      [](Export* a, Export* b) { return std::strcmp(a->name, b->name) < 0; });
}

Export* ExportResolver::GetExportByOrdinal(const std::string_view module_name,
                                           uint16_t ordinal) {
  static int trace_count = 0;
  if (trace_count < 5) {
    fprintf(stderr, "[TRACE] GetExportByOrdinal('%.*s', %u) tables_.size()=%zu this=%p\n",
            (int)module_name.size(), module_name.data(), ordinal, tables_.size(), (void*)this); fflush(stderr);
    for (size_t ti = 0; ti < tables_.size(); ti++) {
      fprintf(stderr, "[TRACE]   table[%zu] name='%s' ordinal_count=%zu\n",
              ti, tables_[ti].module_name().c_str(), tables_[ti].exports_by_ordinal().size()); fflush(stderr);
    }
    trace_count++;
  }
  for (const auto& table : tables_) {
    if (rex::string::utf8_starts_with_case(module_name, table.module_name())) {
      if (ordinal >= table.exports_by_ordinal().size()) {
        return nullptr;
      }
      return table.exports_by_ordinal().at(ordinal);
    }
  }
  return nullptr;
}

void ExportResolver::SetVariableMapping(const std::string_view module_name,
                                        uint16_t ordinal, uint32_t value) {
  auto export_entry = GetExportByOrdinal(module_name, ordinal);
  assert_not_null(export_entry);
  export_entry->tags |= ExportTag::kImplemented;
  export_entry->variable_ptr = value;
}

}  // namespace rex::runtime
