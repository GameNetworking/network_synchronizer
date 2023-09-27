#include "snapshot.h"

#include "scene/main/node.h"
#include "scene_synchronizer.h"

NetUtility::Snapshot::operator String() const {
	String s;
	s += "Snapshot input ID: " + itos(input_id);

	for (int net_node_id = 0; net_node_id < node_vars.size(); net_node_id += 1) {
		s += "\nNode Data: " + itos(net_node_id);
		for (int i = 0; i < node_vars[net_node_id].size(); i += 1) {
			s += "\n|- Variable: ";
			s += node_vars[net_node_id][i].name;
			s += " = ";
			s += String(node_vars[net_node_id][i].value);
		}
	}
	s += "\nCUSTOM DATA:\n";
	for (int i = 0; i < custom_data.size(); i += 1) {
		s += " - " + itos(i) + ": " + String(custom_data[i]);
	}
	return s;
}

bool compare_vars(
		NS::SceneSynchronizerBase &scene_synchronizer,
		const NetUtility::NodeData *p_synchronizer_node_data,
		const Vector<NetUtility::Var> &p_server_vars,
		const Vector<NetUtility::Var> &p_client_vars,
		NetUtility::Snapshot *r_no_rewind_recover,
		LocalVector<String> *r_differences_info) {
	const NetUtility::Var *s_vars = p_server_vars.ptr();
	const NetUtility::Var *c_vars = p_client_vars.ptr();

#ifdef DEBUG_ENABLED
	bool is_equal = true;
#endif

	for (uint32_t var_index = 0; var_index < uint32_t(p_client_vars.size()); var_index += 1) {
		if (uint32_t(p_server_vars.size()) <= var_index) {
			// This variable isn't defined into the server snapshot, so assuming it's correct.
			continue;
		}

		if (s_vars[var_index].name == StringName()) {
			// This variable was not set, skip the check.
			continue;
		}

		// Compare.
		const bool different =
				// Make sure this variable is set.
				c_vars[var_index].name == StringName() ||
				// Check if the value is different.
				!scene_synchronizer.compare(
						s_vars[var_index].value,
						c_vars[var_index].value);

		if (different) {
			if (p_synchronizer_node_data->vars[var_index].skip_rewinding) {
				// The vars are different, but we don't need to trigger a rewind.
				if (r_no_rewind_recover) {
					if (uint32_t(r_no_rewind_recover->node_vars.ptr()[p_synchronizer_node_data->id].size()) <= var_index) {
						r_no_rewind_recover->node_vars.ptrw()[p_synchronizer_node_data->id].resize(var_index + 1);
					}
					r_no_rewind_recover->node_vars.ptrw()[p_synchronizer_node_data->id].ptrw()[var_index] = s_vars[var_index];
					// Sets `input_id` to 0 to signal that this snapshot contains
					// no-rewind data.
					r_no_rewind_recover->input_id = 0;
				}

				if (r_differences_info) {
					r_differences_info->push_back(
							"[NO REWIND] Difference found on var #" + itos(var_index) + " " + p_synchronizer_node_data->vars[var_index].var.name + " " +
							"Server value: `" + NetUtility::stringify_fast(s_vars[var_index].value) + "` " +
							"Client value: `" + NetUtility::stringify_fast(c_vars[var_index].value) + "`.    " +
							"[Server name: `" + s_vars[var_index].name + "` " +
							"Client name: `" + c_vars[var_index].name + "`].");
				}
			} else {
				// The vars are different.
				if (r_differences_info) {
					r_differences_info->push_back(
							"Difference found on var #" + itos(var_index) + " " + p_synchronizer_node_data->vars[var_index].var.name + " " +
							"Server value: `" + NetUtility::stringify_fast(s_vars[var_index].value) + "` " +
							"Client value: `" + NetUtility::stringify_fast(c_vars[var_index].value) + "`.    " +
							"[Server name: `" + s_vars[var_index].name + "` " +
							"Client name: `" + c_vars[var_index].name + "`].");
				}
#ifdef DEBUG_ENABLED
				is_equal = false;
#else
				return false;
#endif
			}
		}
	}

#ifdef DEBUG_ENABLED
	return is_equal;
#else
	return true;
#endif
}

bool NetUtility::Snapshot::compare(
		NS::SceneSynchronizerBase &scene_synchronizer,
		const Snapshot &p_snap_A,
		const Snapshot &p_snap_B,
		Snapshot *r_no_rewind_recover,
		LocalVector<String> *r_differences_info
#ifdef DEBUG_ENABLED
		,
		LocalVector<NetNodeId> *r_different_node_data
#endif
) {
#ifdef DEBUG_ENABLED
	bool is_equal = true;
#endif

	if (p_snap_A.custom_data.size() != p_snap_B.custom_data.size()) {
		if (r_differences_info) {
			r_differences_info->push_back("Difference detected: custom data is different.");
		}
#ifdef DEBUG_ENABLED
		is_equal = false;
#else
		return false;
#endif
	} else {
		for (int i = 0; i < p_snap_A.custom_data.size(); i++) {
			if (!scene_synchronizer.compare(p_snap_A.custom_data[i], p_snap_B.custom_data[i])) {
				if (r_differences_info) {
					r_differences_info->push_back("Difference detected: custom_data is different at index `" + itos(i) + "`.");
				}
#ifdef DEBUG_ENABLED
				is_equal = false;
#else
				return false;
#endif
			}
		}
	}

	if (r_no_rewind_recover) {
		r_no_rewind_recover->node_vars.resize(MAX(p_snap_A.node_vars.size(), p_snap_B.node_vars.size()));
	}

	for (NetNodeId net_node_id = 0; net_node_id < uint32_t(p_snap_A.node_vars.size()); net_node_id += 1) {
		NetUtility::NodeData *rew_node_data = scene_synchronizer.get_node_data(net_node_id);
		if (rew_node_data == nullptr || rew_node_data->realtime_sync_enabled_on_client == false) {
			continue;
		}

		bool are_nodes_different = false;
		if (net_node_id >= uint32_t(p_snap_B.node_vars.size())) {
			if (r_differences_info) {
				r_differences_info->push_back("Difference detected: The B snapshot doesn't contain this node: " + String(rew_node_data->object_name.c_str()));
			}
#ifdef DEBUG_ENABLED
			is_equal = false;
#else
			return false;
#endif
			are_nodes_different = true;
		} else {
			are_nodes_different = !compare_vars(
					scene_synchronizer,
					rew_node_data,
					p_snap_A.node_vars[net_node_id],
					p_snap_B.node_vars[net_node_id],
					r_no_rewind_recover,
					r_differences_info);

			if (are_nodes_different) {
				if (r_differences_info) {
					r_differences_info->push_back("Difference detected: The node status on snapshot B is different. NODE: " + String(rew_node_data->object_name.c_str()));
				}
#ifdef DEBUG_ENABLED
				is_equal = false;
#else
				return false;
#endif
			}
		}

#ifdef DEBUG_ENABLED
		if (are_nodes_different && r_different_node_data) {
			r_different_node_data->push_back(net_node_id);
		}
#endif
	}

#ifdef DEBUG_ENABLED
	return is_equal;
#else
	return true;
#endif
}