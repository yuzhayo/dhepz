#include "modules/gate/module_validator.h"

#include <algorithm>
#include <set>
#include <utility>

namespace modules {
namespace {

bool SameUniqueStrings(const std::vector<std::wstring>& left,
                       const std::vector<std::wstring>& right) {
  const std::set<std::wstring> left_set(left.begin(), left.end());
  const std::set<std::wstring> right_set(right.begin(), right.end());
  return left_set.size() == left.size() && right_set.size() == right.size() &&
         left_set == right_set;
}

const json::Value* NodeProperty(const ui::config::ComponentNode& node,
                                std::wstring_view name) {
  for (const auto& [key, value] : node.properties_) {
    if (key == name) return &value;
  }
  return nullptr;
}

void CollectBindings(const json::Value& value,
                     std::set<std::wstring>* bindings) {
  if (value.is_object()) {
    if (const json::Value* binding = value.Find(L"$bind");
        binding != nullptr && binding->is_string() &&
        value.members().size() == 1) {
      bindings->insert(binding->AsString());
      return;
    }
    for (const auto& [key, member] : value.members()) {
      (void)key;
      CollectBindings(member, bindings);
    }
  } else if (value.is_array()) {
    for (const json::Value& item : value.items()) {
      CollectBindings(item, bindings);
    }
  }
}

const json::Value* FindBinding(const json::Value& value,
                               std::wstring_view reference) {
  if (value.is_object()) {
    if (const json::Value* binding = value.Find(L"$bind");
        binding != nullptr && binding->is_string() &&
        value.members().size() == 1 && binding->AsString() == reference) {
      return binding;
    }
    for (const auto& [key, member] : value.members()) {
      (void)key;
      if (const json::Value* found = FindBinding(member, reference)) return found;
    }
  } else if (value.is_array()) {
    for (const json::Value& item : value.items()) {
      if (const json::Value* found = FindBinding(item, reference)) return found;
    }
  }
  return nullptr;
}

void CollectReferences(const ui::config::ComponentNode& node,
                       std::set<std::wstring>* actions,
                       std::set<std::wstring>* bindings,
                       std::set<std::wstring>* styles) {
  for (const auto& [key, value] : node.properties_) {
    CollectBindings(value, bindings);
    if (key == L"action" && value.is_string()) {
      actions->insert(value.AsString());
    } else if (key.find(L"binding") != std::wstring::npos &&
               value.is_string()) {
      std::wstring binding = value.AsString();
      if (!binding.empty() && binding.front() == L'$') binding.erase(binding.begin());
      bindings->insert(std::move(binding));
    } else if (key == L"style" && value.is_string()) {
      styles->insert(value.AsString());
    }
  }
  for (const ui::config::ComponentNode& child : node.children_) {
    CollectReferences(child, actions, bindings, styles);
  }
}

const json::Value* FindReferenceValue(const ui::config::ComponentNode& node,
                                      std::wstring_view kind,
                                      std::wstring_view reference) {
  for (const auto& [key, value] : node.properties_) {
    if (kind == L"binding") {
      if (const json::Value* found = FindBinding(value, reference)) return found;
    }
    if (!value.is_string()) continue;
    bool kind_matches = key == kind;
    if (kind == L"binding") {
      kind_matches = key.find(L"binding") != std::wstring::npos;
    }
    if (!kind_matches) continue;
    std::wstring candidate = value.AsString();
    if (kind == L"binding" && !candidate.empty() &&
        candidate.front() == L'$') {
      candidate.erase(candidate.begin());
    }
    if (candidate == reference) return &value;
  }
  for (const ui::config::ComponentNode& child : node.children_) {
    if (const json::Value* found = FindReferenceValue(child, kind, reference)) {
      return found;
    }
  }
  return nullptr;
}

std::wstring ScreenSourceForModule(
    const std::vector<ui::config::ScreenSource>& sources,
    std::wstring_view module_id) {
  for (const ui::config::ScreenSource& source : sources) {
    json::Value document;
    if (!json::Parse(source.text, &document).ok()) continue;
    const json::Value* components = document.Find(L"components");
    if (components == nullptr || !components->is_array()) continue;
    for (const json::Value& component : components->items()) {
      if (component.StringField(L"module_id") == module_id) return source.name;
    }
  }
  return L"embedded";
}

std::set<std::wstring> SourceModuleIds(
    const ui::config::ScreenSource& source) {
  std::set<std::wstring> ids;
  json::Value document;
  if (!json::Parse(source.text, &document).ok()) return ids;
  const json::Value* components = document.Find(L"components");
  if (components == nullptr || !components->is_array()) return ids;
  for (const json::Value& component : components->items()) {
    const std::wstring id = component.StringField(L"module_id");
    if (!id.empty()) ids.insert(id);
  }
  return ids;
}

bool StyleExists(const json::Value& core, std::wstring_view style) {
  const json::Value* styles = core.ObjectField(L"styles");
  return styles != nullptr && styles->Find(style) != nullptr;
}

ui::config::ScreenSource FilterAcceptedSource(
    const ui::config::ScreenSource& source,
    const std::set<std::wstring>& accepted_modules) {
  json::Value document;
  if (!json::Parse(source.text, &document).ok()) return source;
  const json::Value* components = document.Find(L"components");
  if (components == nullptr || !components->is_array()) return source;
  json::Value filtered = json::Value::Object();
  json::Value kept = json::Value::Array();
  for (const json::Value& component : components->items()) {
    const std::wstring module_id = component.StringField(L"module_id");
    if (module_id.empty() || accepted_modules.contains(module_id)) {
      kept.Append(component);
    }
  }
  filtered.Set(L"components", std::move(kept));
  return {source.name, json::Serialize(filtered)};
}

ui::config::ScreenSource FilterWithdrawnSource(
    const ui::config::ScreenSource& source, std::wstring_view module_id) {
  json::Value document;
  if (!json::Parse(source.text, &document).ok()) return source;
  const json::Value* components = document.Find(L"components");
  if (components == nullptr || !components->is_array()) return source;
  json::Value filtered = json::Value::Object();
  json::Value kept = json::Value::Array();
  for (const json::Value& component : components->items()) {
    if (component.StringField(L"module_id") != module_id) kept.Append(component);
  }
  filtered.Set(L"components", std::move(kept));
  return {source.name, json::Serialize(filtered)};
}

core::Status ParseEnvelope(
    std::wstring_view embedded_text, std::wstring_view override_text,
    json::Value* envelope, json::Value* core_catalog,
    std::vector<ui::config::ScreenSource>* sources,
    std::size_t* shipped_source_count) {
  DHEPZ_RETURN_IF_ERROR(json::Parse(embedded_text, envelope));
  const json::Value* core = envelope->Find(L"core");
  json::Value parsed_core;
  if (const json::Value* core_text = envelope->Find(L"coreText");
      core_text != nullptr && core_text->is_string()) {
    DHEPZ_RETURN_IF_ERROR(json::Parse(core_text->AsString(), &parsed_core));
    core = &parsed_core;
  }
  if (core == nullptr || !core->is_object()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Embedded document carries no core catalog");
  }
  *core_catalog = *core;

  if (const json::Value* entries = envelope->Find(L"sources");
      entries != nullptr && entries->is_array()) {
    for (const json::Value& entry : entries->items()) {
      const json::Value* file = entry.Find(L"file");
      const json::Value* text = entry.Find(L"text");
      if (file == nullptr || !file->is_string() || text == nullptr ||
          !text->is_string()) {
        return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                         L"Embedded screen source requires file and text");
      }
      sources->push_back({file->AsString(), text->AsString()});
    }
  } else {
    json::Value screens = json::Value::Object();
    if (const json::Value* components = envelope->Find(L"components")) {
      screens.Set(L"components", *components);
    }
    sources->push_back({L"embedded", json::Serialize(screens)});
  }
  if (sources->empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Embedded document carries no screen sources");
  }
  *shipped_source_count = sources->size();
  if (!override_text.empty()) {
    sources->push_back({L"override", std::wstring(override_text)});
  }
  return core::Ok();
}

}  // namespace

core::Status ModuleValidator::Validate(
    std::wstring_view embedded_text, std::wstring_view override_text,
    const std::vector<RegisteredModule>& registered,
    ModuleValidationResult* out) const {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Module validation output is required");
  }
  *out = {};
  json::Value envelope;
  std::vector<ui::config::ScreenSource> sources;
  std::size_t shipped_source_count = 0;
  DHEPZ_RETURN_IF_ERROR(ParseEnvelope(embedded_text, override_text, &envelope,
                                      &out->core_catalog, &sources,
                                      &shipped_source_count));

  // A schema-broken module source is quarantined before the combined resolve,
  // so one bad child cannot remove the parent shell or healthy siblings.
  std::set<std::wstring> schema_rejected;
  std::vector<ui::config::ScreenSource> resolvable_sources;
  std::size_t resolvable_shipped_count = 0;
  for (std::size_t source_index = 0; source_index < sources.size();
       ++source_index) {
    const ui::config::ScreenSource& source = sources[source_index];
    const std::set<std::wstring> ids = SourceModuleIds(source);
    if (source_index < shipped_source_count && ids.size() == 1) {
      std::vector<ui::config::Diagnostic> diagnostics;
      std::unique_ptr<ui::config::ResolvedUiDocument> isolated;
      const core::Status valid = ui::config::ResolveDocument(
          out->core_catalog, {source}, &diagnostics, &isolated);
      if (!valid.ok()) {
        const ui::config::Diagnostic diagnostic =
            diagnostics.empty() ? ui::config::Diagnostic{} : diagnostics.front();
        const std::wstring id = *ids.begin();
        out->rejects.push_back(
            {id,
             diagnostic.message.empty() ? valid.Message() : diagnostic.message,
             source.name, diagnostic.line > 0 ? diagnostic.line : 1,
             diagnostic.column > 0 ? diagnostic.column : 1,
             DiagnosticStage::Pairing});
        schema_rejected.insert(id);
        continue;
      }
    }
    resolvable_sources.push_back(source);
    if (source_index < shipped_source_count) ++resolvable_shipped_count;
  }

  std::vector<ui::config::Diagnostic> ui_diagnostics;
  std::unique_ptr<ui::config::ResolvedUiDocument> candidate_document;
  const core::Status resolved = ui::config::ResolveDocument(
      out->core_catalog, resolvable_sources, &ui_diagnostics,
      &candidate_document);
  if (!resolved.ok()) return resolved;

  const json::Value* manifests = envelope.Find(L"modules");
  std::set<std::wstring> manifest_ids;
  std::set<std::wstring> accepted_ids;
  bool settings_all_claimed = false;
  bool config_write_claimed = false;

  if (manifests != nullptr && manifests->is_array()) {
    for (const json::Value& manifest_entry : manifests->items()) {
      json::Value parsed_manifest;
      const json::Value* manifest_value = &manifest_entry;
      std::wstring manifest_file = L"embedded";
      std::wstring manifest_text = json::Serialize(manifest_entry);
      if (const json::Value* raw_text = manifest_entry.Find(L"text");
          raw_text != nullptr && raw_text->is_string()) {
        if (const json::Value* file = manifest_entry.Find(L"file");
            file != nullptr && file->is_string()) {
          manifest_file = file->AsString();
        }
        manifest_text = raw_text->AsString();
        const core::Status parsed = json::Parse(manifest_text, &parsed_manifest);
        if (!parsed.ok()) {
          out->rejects.push_back(
              {L"?", L"manifest invalid: " + parsed.Message(), manifest_file, 1, 1,
               DiagnosticStage::Manifest});
          continue;
        }
        manifest_value = &parsed_manifest;
      }

      const json::Value* raw_id = manifest_value->Find(L"moduleId");
      const std::wstring hinted_id =
          raw_id != nullptr && raw_id->is_string() ? raw_id->AsString() : L"?";
      manifest_ids.insert(hinted_id);
      if (schema_rejected.contains(hinted_id)) continue;

      ModuleManifest manifest;
      std::vector<ManifestDiagnostic> manifest_diagnostics;
      if (!ParseManifest(manifest_text, &manifest, &manifest_diagnostics).ok()) {
        const ManifestDiagnostic diagnostic = manifest_diagnostics.empty()
                                                  ? ManifestDiagnostic{}
                                                  : manifest_diagnostics.front();
        out->rejects.push_back(
            {hinted_id,
             L"manifest invalid: " +
                 (diagnostic.message.empty() ? L"unknown error"
                                             : diagnostic.message),
             manifest_file, diagnostic.line > 0 ? diagnostic.line : 1,
             diagnostic.column > 0 ? diagnostic.column : 1,
             DiagnosticStage::Manifest});
        continue;
      }
      const int manifest_line = raw_id != nullptr ? raw_id->line() : 1;
      const int manifest_column = raw_id != nullptr ? raw_id->column() : 1;
      auto reject = [&](std::wstring reason, std::wstring file = {}, int line = 0,
                        int column = 0,
                        DiagnosticStage stage = DiagnosticStage::Pairing) {
        out->rejects.push_back(
            {manifest.module_id, std::move(reason),
             file.empty() ? manifest_file : std::move(file),
             line > 0 ? line : manifest_line,
             column > 0 ? column : manifest_column, stage});
      };

      const RegisteredModule* registration = nullptr;
      for (const RegisteredModule& item : registered) {
        if (item.module_id == manifest.module_id) {
          registration = &item;
          break;
        }
      }
      if (registration == nullptr) {
        reject(L"logic half not self-registered");
        continue;
      }

      std::vector<const ui::config::Route*> module_routes;
      for (const ui::config::Route& route : candidate_document->routes()) {
        if (route.root.GetString(L"module_id") == manifest.module_id) {
          module_routes.push_back(&route);
        }
      }
      if (module_routes.empty()) {
        reject(L"no screen half with matching module_id");
        continue;
      }

      std::unique_ptr<ModuleDescriptor> descriptor = registration->factory();
      const std::wstring screen_file =
          ScreenSourceForModule(resolvable_sources, manifest.module_id);
      if (descriptor->ModuleId() != manifest.module_id ||
          descriptor->TabLabel() != manifest.tab_label ||
          descriptor->Order() != manifest.order ||
          descriptor->ShowInTabs() != manifest.show_in_tabs ||
          descriptor->SettingsRoute() != manifest.settings_route) {
        reject(L"descriptor metadata does not match module.json");
        continue;
      }
      const std::vector<std::wstring> code_actions =
          descriptor->DeclaredActions();
      if (!SameUniqueStrings(code_actions, manifest.actions)) {
        std::wstring reason = L"DeclaredActions() contains duplicates";
        for (const std::wstring& action : manifest.actions) {
          if (std::find(code_actions.begin(), code_actions.end(), action) ==
              code_actions.end()) {
            reason = L"action '" + action +
                     L"' declared in module.json has no C++ handler";
            break;
          }
        }
        if (reason.find(L"duplicates") != std::wstring::npos) {
          for (const std::wstring& action : code_actions) {
            if (std::find(manifest.actions.begin(), manifest.actions.end(), action) ==
                manifest.actions.end()) {
              reason = L"C++ action '" + action +
                       L"' is not declared in module.json";
              break;
            }
          }
        }
        reject(std::move(reason));
        continue;
      }
      if (!SameUniqueStrings(descriptor->DeclaredBindings(), manifest.bindings)) {
        reject(L"DeclaredBindings() does not match module.json");
        continue;
      }
      if (!SameUniqueStrings(descriptor->DeclaredCapabilities(),
                             manifest.capabilities)) {
        reject(L"DeclaredCapabilities() does not match module.json");
        continue;
      }

      const ui::config::Route* primary = module_routes.front();
      if ((primary->root.Has(L"tab_label") &&
           primary->tab_label != manifest.tab_label) ||
          primary->show_in_tabs != manifest.show_in_tabs) {
        const json::Value* at = primary->root.Has(L"tab_label")
                                    ? NodeProperty(primary->root, L"tab_label")
                                    : NodeProperty(primary->root, L"show_in_tabs");
        reject(L"screen metadata does not match module.json", screen_file,
               at != nullptr ? at->line() : 1,
               at != nullptr ? at->column() : 1);
        continue;
      }

      std::set<std::wstring> referenced_actions;
      std::set<std::wstring> referenced_bindings;
      std::set<std::wstring> referenced_styles;
      for (const ui::config::Route* route : module_routes) {
        CollectReferences(route->root, &referenced_actions, &referenced_bindings,
                          &referenced_styles);
      }
      const std::set<std::wstring> action_set(manifest.actions.begin(),
                                              manifest.actions.end());
      const std::set<std::wstring> binding_set(manifest.bindings.begin(),
                                               manifest.bindings.end());
      bool references_ok = true;
      for (const std::wstring& action : referenced_actions) {
        if (!action_set.contains(action)) {
          const json::Value* at =
              FindReferenceValue(primary->root, L"action", action);
          reject(L"screen references undeclared action '" + action + L"'",
                 screen_file, at != nullptr ? at->line() : 1,
                 at != nullptr ? at->column() : 1);
          references_ok = false;
          break;
        }
      }
      if (references_ok) {
        for (const std::wstring& binding : referenced_bindings) {
          if (!binding.empty() && !binding_set.contains(binding)) {
            const json::Value* at =
                FindReferenceValue(primary->root, L"binding", binding);
            reject(L"screen references undeclared binding '" + binding + L"'",
                   screen_file, at != nullptr ? at->line() : 1,
                   at != nullptr ? at->column() : 1);
            references_ok = false;
            break;
          }
        }
      }
      if (references_ok) {
        for (const std::wstring& style : referenced_styles) {
          if (!style.empty() && !StyleExists(out->core_catalog, style)) {
            const json::Value* at =
                FindReferenceValue(primary->root, L"style", style);
            reject(L"screen references unknown style '" + style + L"'",
                   screen_file, at != nullptr ? at->line() : 1,
                   at != nullptr ? at->column() : 1);
            references_ok = false;
            break;
          }
        }
      }
      if (!references_ok) continue;

      bool settings_all_granted = false;
      bool config_write_granted = false;
      const json::Value* capabilities = manifest_value->Find(L"capabilities");
      const int capability_line = capabilities != nullptr ? capabilities->line() : 1;
      const int capability_column =
          capabilities != nullptr ? capabilities->column() : 1;
      bool capability_ok = true;
      for (const std::wstring& capability : manifest.capabilities) {
        if (capability == kCapabilitySettingsAll) {
          if (manifest.module_id != L"settings") {
            reject(L"settings:all is reserved for module 'settings'",
                   manifest_file, capability_line, capability_column,
                   DiagnosticStage::Capability);
            capability_ok = false;
            break;
          }
          if (settings_all_claimed) {
            reject(L"settings:all already claimed by module 'settings'",
                   manifest_file, capability_line, capability_column,
                   DiagnosticStage::Capability);
            capability_ok = false;
            break;
          }
          settings_all_granted = true;
        } else if (capability == kCapabilityConfigWrite) {
          if (manifest.module_id != L"ui-editor") {
            reject(L"config:write is reserved for module 'ui-editor'",
                   manifest_file, capability_line, capability_column,
                   DiagnosticStage::Capability);
            capability_ok = false;
            break;
          }
          if (config_write_claimed) {
            reject(L"config:write already claimed by module 'ui-editor'",
                   manifest_file, capability_line, capability_column,
                   DiagnosticStage::Capability);
            capability_ok = false;
            break;
          }
          config_write_granted = true;
        } else {
          reject(L"unknown capability '" + capability + L"'", manifest_file,
                 capability_line, capability_column,
                 DiagnosticStage::Capability);
          capability_ok = false;
          break;
        }
      }
      if (!capability_ok) continue;

      if (!manifest.settings_route.empty()) {
        const ui::config::Route* settings_route =
            candidate_document->FindRoute(manifest.settings_route);
        if (settings_route == nullptr ||
            settings_route->root.GetString(L"module_id") != manifest.module_id) {
          reject(L"settingsRoute not shipped by this module");
          continue;
        }
      }

      bool actions_unique = true;
      for (const std::wstring& action : manifest.actions) {
        for (const auto& [existing, owner] : out->action_map) {
          if (existing == action) {
            reject(L"action '" + action + L"' already registered by " + owner);
            actions_unique = false;
            break;
          }
        }
        if (!actions_unique) break;
      }
      if (!actions_unique) continue;

      if (settings_all_granted) settings_all_claimed = true;
      if (config_write_granted) config_write_claimed = true;
      for (const std::wstring& action : manifest.actions) {
        out->action_map.emplace_back(action, manifest.module_id);
      }
      if (settings_all_granted) {
        out->grants.push_back({manifest.module_id,
                              std::wstring(kCapabilitySettingsAll), manifest_file,
                              capability_line, capability_column});
      }
      if (config_write_granted) {
        out->grants.push_back({manifest.module_id,
                              std::wstring(kCapabilityConfigWrite), manifest_file,
                              capability_line, capability_column});
      }
      accepted_ids.insert(manifest.module_id);
      out->modules.push_back({std::move(manifest), std::move(descriptor),
                              settings_all_granted, config_write_granted});
    }
  }

  std::set<std::wstring> screen_module_ids;
  for (const ui::config::Route& route : candidate_document->routes()) {
    const std::wstring id = route.root.GetString(L"module_id");
    if (!id.empty()) screen_module_ids.insert(id);
  }
  for (const std::wstring& id : screen_module_ids) {
    if (!manifest_ids.contains(id)) {
      out->rejects.push_back({id, L"screen half has no module.json manifest",
                              ScreenSourceForModule(resolvable_sources, id), 1, 1,
                              DiagnosticStage::Pairing});
    }
  }
  for (const RegisteredModule& registration : registered) {
    if (!manifest_ids.contains(registration.module_id) &&
        !screen_module_ids.contains(registration.module_id)) {
      out->rejects.push_back({registration.module_id,
                              L"logic half has no module.json manifest",
                              L"registry", 1, 1,
                              DiagnosticStage::Pairing});
    }
  }

  std::vector<ui::config::ScreenSource> accepted_sources;
  accepted_sources.reserve(resolvable_sources.size());
  for (const ui::config::ScreenSource& source : resolvable_sources) {
    accepted_sources.push_back(FilterAcceptedSource(source, accepted_ids));
  }
  std::vector<ui::config::Diagnostic> accepted_diagnostics;
  DHEPZ_RETURN_IF_ERROR(ui::config::ResolveDocument(
      out->core_catalog, accepted_sources, &accepted_diagnostics, &out->document));

  json::Value accepted_document = json::Value::Object();
  json::Value accepted_components = json::Value::Array();
  for (std::size_t index = 0;
       index < accepted_sources.size() && index < resolvable_shipped_count;
       ++index) {
    json::Value source_document;
    DHEPZ_RETURN_IF_ERROR(
        json::Parse(accepted_sources[index].text, &source_document));
    if (const json::Value* components = source_document.Find(L"components");
        components != nullptr && components->is_array()) {
      for (const json::Value& component : components->items()) {
        accepted_components.Append(component);
      }
    }
  }
  accepted_document.Set(L"components", std::move(accepted_components));
  out->accepted_base = {L"embedded", json::Serialize(accepted_document)};
  out->accepted_sources = accepted_sources;
  return core::Ok();
}

core::Status ModuleValidator::WithdrawModule(
    const json::Value& core_catalog,
    const std::vector<ui::config::ScreenSource>& current_sources,
    std::wstring_view module_id,
    std::vector<ui::config::ScreenSource>* filtered_sources,
    std::unique_ptr<ui::config::ResolvedUiDocument>* document) const {
  if (filtered_sources == nullptr || document == nullptr || module_id.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"WithdrawModule requires outputs and a module id");
  }
  filtered_sources->clear();
  filtered_sources->reserve(current_sources.size());
  for (const ui::config::ScreenSource& source : current_sources) {
    filtered_sources->push_back(FilterWithdrawnSource(source, module_id));
  }
  std::vector<ui::config::Diagnostic> diagnostics;
  return ui::config::ResolveDocument(core_catalog, *filtered_sources,
                                     &diagnostics, document);
}

}  // namespace modules
