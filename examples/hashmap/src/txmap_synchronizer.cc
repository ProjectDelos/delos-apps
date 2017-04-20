#include <txmap_synchronizer.h>
#include <cstring>
#include <iostream>
#include <fstream>
#include <cassert>
#include <thread>
#include <algorithm>
#include <atomicmap.h>

static char* txn_file = "txns.txt";
static char* val_file = "val.txt";

TXMapSynchronizer::TXMapSynchronizer(std::vector<std::string>* log_addr, std::vector<ColorID>& interesting_colors, Context* context, bool replication): m_context(context), m_replication(replication) {
        assert(interesting_colors.size() == 2);
        // local color
        m_local_color = static_cast<struct colors*>(malloc(sizeof(struct colors)));
        m_local_color->numcolors = 1;
        m_local_color->mycolors = static_cast<ColorID*>(malloc(sizeof(ColorID)));
        m_local_color->mycolors[0] = interesting_colors[0];
        // remote color
        m_remote_color = static_cast<struct colors*>(malloc(sizeof(struct colors)));
        m_remote_color->numcolors = 1;
        m_remote_color->mycolors = static_cast<ColorID*>(malloc(sizeof(ColorID)));
        m_remote_color->mycolors[0] = interesting_colors[1];
    //  }
        this->m_interesting_colors = m_local_color;
        // Initialize fuzzylog connection
        if (m_replication) {
                assert (log_addr->size() > 0 && log_addr->size() % 2 == 0);
                size_t num_chain_servers = log_addr->size() / 2;
                const char *chain_server_head_ips[num_chain_servers]; 
                for (auto i = 0; i < num_chain_servers; i++) {
                        chain_server_head_ips[i] = log_addr->at(i).c_str();
                }
                const char *chain_server_tail_ips[num_chain_servers]; 
                for (auto i = 0; i < num_chain_servers; i++) {
                        chain_server_tail_ips[i] = log_addr->at(num_chain_servers+i).c_str();
                }
                m_fuzzylog_client = new_dag_handle_with_replication(num_chain_servers, chain_server_head_ips, chain_server_tail_ips, m_interesting_colors);

        } else {
                size_t num_chain_servers = log_addr->size();
                const char *chain_server_ips[num_chain_servers]; 
                for (auto i = 0; i < num_chain_servers; i++) {
                        chain_server_ips[i] = log_addr->at(i).c_str();
                }
                m_fuzzylog_client = new_dag_handle_with_skeens(num_chain_servers, chain_server_ips, m_interesting_colors);
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
        this->m_running = true;
}

void TXMapSynchronizer::run() {
        int err;
        err = pthread_create(&m_thread, NULL, bootstrap, this);
        assert(err == 0);
}

void TXMapSynchronizer::join() {
        m_running = false; 
        pthread_join(m_thread, NULL);
        close_dag_handle(m_fuzzylog_client);
}

void* TXMapSynchronizer::bootstrap(void *arg) {
        TXMapSynchronizer *synchronizer= static_cast<TXMapSynchronizer*>(arg);
        synchronizer->Execute();
        return NULL;
}

void TXMapSynchronizer::Execute() {
        std::cout << "Start TXMap synchronizer..." << std::endl;
        size_t size = 0;
        size_t locs_read = 0;
        get_next_val next_val;
        LocationInColor commit_version;

        bool needs_buffering = false;

        while (m_running) {
                // m_pending_queue ==> m_current_queue
          //      swap_queue();

                // update local map
                {
                        //std::lock_guard<std::mutex> lock(m_local_map_mtx);
                        snapshot(m_fuzzylog_client);
                        while (true) {
                                next_val = get_next2(m_fuzzylog_client, &size, &locs_read);
                                if (locs_read == 0) break;
                                //assert(next_val.locs[0].entry == next_val.locs[1].entry);
                                commit_version = std::max(next_val.locs[0].entry, next_val.locs[1].entry);
                                // readset, writeset
                                txmap_node node;
                                node = *reinterpret_cast<txmap_node*>(const_cast<uint8_t*>(next_val.data));
                                if (node.node_type == txmap_node::NodeType::COMMIT_RECORD) {
                                        assert(locs_read == 2 || locs_read == 1);
                                        txmap_commit_node commit_node;
                                        // Read commit record
                                        deserialize_commit_record(const_cast<uint8_t*>(next_val.data), size, &commit_node);

                                        if (needs_buffering) {
                                                // buffer node
                                                buffer_commit_node(commit_node.clone(), commit_version);
                                                continue;
                                        }
                                        if (!is_decision_possible(&commit_node)) {
                                                // buffer node
                                                buffer_commit_node(commit_node.clone(), commit_version);
                                                needs_buffering = true;                
                                                continue;
                                        }

                                        // Validate
                                        bool valid = validate_txn(&commit_node);
                                        if (valid) {
                                                update_map(&commit_node, commit_version);
                                                m_context->inc_num_committed();

                                        } else {
                                                m_context->inc_num_aborted();
                                        }

                                        // Append decision node to remote color if this is not local only commit
                                        if (!is_local_only_txn(&commit_node)) 
                                                append_decision_node_to_remote(commit_version, valid);

                                } else if (node.node_type == txmap_node::NodeType::DECISION_RECORD) {
                                        assert(locs_read == 1);
                                        txmap_decision_node decision_node;
                                        // Read decision record
                                        deserialize_decision_record(const_cast<uint8_t*>(next_val.data), size, &decision_node);
                                        needs_buffering = apply_buffered_nodes(&decision_node);
                                }
                        }
                }
                // Wake up all waiting worker threads
         //     while (!m_current_queue.empty()) {
         //             std::condition_variable* cv = m_current_queue.front().first;
         //             std::atomic_bool* cv_spurious_wake_up = m_current_queue.front().second;
         //             assert(*cv_spurious_wake_up == true);
         //             *cv_spurious_wake_up = false;
         //             cv->notify_one();
         //             m_current_queue.pop();
         //     } 
                std::this_thread::sleep_for(std::chrono::nanoseconds(1));
        }
}

void TXMapSynchronizer::enqueue_get(std::condition_variable* cv, std::atomic_bool* cv_spurious_wake_up) {
        std::lock_guard<std::mutex> lock(m_queue_mtx);
        m_pending_queue.push(std::make_pair(cv, cv_spurious_wake_up));
}

void TXMapSynchronizer::swap_queue() {
        std::lock_guard<std::mutex> lock(m_queue_mtx);
        std::swap(m_pending_queue, m_current_queue);
}

std::mutex* TXMapSynchronizer::get_local_map_lock() {
        return &m_local_map_mtx;
}

uint64_t TXMapSynchronizer::get(uint64_t key) {
        uint64_t value = 0;
        {
                std::lock_guard<std::mutex> lock(m_local_map_mtx);
                if (m_local_map.count(key) > 0)
                        value = m_local_map[key];
        }
        return value;
}

void TXMapSynchronizer::put(uint64_t key, uint64_t value) {
        std::lock_guard<std::mutex> lock(m_local_map_mtx);
        m_local_map[key] = value;
}

void TXMapSynchronizer::deserialize_commit_record(uint8_t *in, size_t size, txmap_commit_node* commit_node) {
        // deserialize read set
        uint32_t offset;
        txmap_record *buf_record;
        txmap_node *buf_node;
        txmap_set *buf_rset;
        txmap_set *rset, *wset;
        rset = &commit_node->read_set;
        wset = &commit_node->write_set;
        
        offset = 0;
        buf_node = reinterpret_cast<txmap_node*>(in + offset);
        assert(buf_node->node_type == txmap_node::NodeType::COMMIT_RECORD);
        commit_node->node.node_type = buf_node->node_type;
        offset += sizeof(txmap_node);

        buf_rset = reinterpret_cast<txmap_set*>(in + offset);
        rset->num_entry = buf_rset->num_entry;
        offset += sizeof(uint32_t);
        rset->set = static_cast<txmap_record*>(malloc(sizeof(txmap_record)*rset->num_entry));
        for (size_t i = 0; i < rset->num_entry; i++) {
                buf_record = reinterpret_cast<txmap_record*>(in + offset); 
                rset->set[i] = *buf_record;
                offset += sizeof(txmap_record);
        }
        // deserialize write set 
        txmap_set *buf_wset;
        buf_wset = reinterpret_cast<txmap_set*>(in + offset);
        wset->num_entry = buf_wset->num_entry;
        offset += sizeof(uint32_t);
        wset->set = static_cast<txmap_record*>(malloc(sizeof(txmap_record)*wset->num_entry));
        for (size_t i = 0; i < wset->num_entry; i++) {
                buf_record = reinterpret_cast<txmap_record*>(in + offset); 
                wset->set[i] = *buf_record; 
                offset += sizeof(txmap_record);
        }
        assert(offset == size);
}

bool TXMapSynchronizer::is_decision_possible(txmap_commit_node *commit_node) {
        txmap_set *rset;
        rset = &commit_node->read_set;
        assert(rset != NULL);
        assert(rset->num_entry == 1);
        txmap_record &r = rset->set[0]; 
        // XXX Rule: every keys in read set should be in local key range
        return is_local_key(r.key);
}

void TXMapSynchronizer::append_decision_node_to_remote(LocationInColor commit_version, bool committed) {
        txmap_decision_node decision_node; 
        decision_node.node.node_type = txmap_node::NodeType::DECISION_RECORD;
        decision_node.commit_version = commit_version;
        decision_node.decision = committed ? txmap_decision_node::DecisionType::COMMITTED : txmap_decision_node::DecisionType::ABORTED;
        size_t size = 0;
        serialize_decision_record(&decision_node, m_write_buf, &size);
        async_append(m_fuzzylog_client, m_write_buf, size, m_remote_color, NULL);
        // DEBUG ==========
        log(txn_file, "APPEND DECISION RECORD", reinterpret_cast<txmap_node*>(&decision_node));
}


void TXMapSynchronizer::serialize_decision_record(txmap_decision_node *decision_node, char* out, size_t* out_size) {
        txmap_decision_node *buf_decision_node;
        buf_decision_node = reinterpret_cast<txmap_decision_node*>(out);
        buf_decision_node->node.node_type = decision_node->node.node_type;
        buf_decision_node->commit_version = decision_node->commit_version;
        buf_decision_node->decision = decision_node->decision;
        *out_size = sizeof(txmap_decision_node);
}

void TXMapSynchronizer::deserialize_decision_record(uint8_t *in, size_t size, txmap_decision_node *decision_node) {
        txmap_decision_node *buf_decision_node;
        buf_decision_node = reinterpret_cast<txmap_decision_node*>(in);
        assert(buf_decision_node->node.node_type == txmap_node::NodeType::DECISION_RECORD);
        decision_node->node.node_type = buf_decision_node->node.node_type;
        decision_node->commit_version = buf_decision_node->commit_version;
        decision_node->decision = buf_decision_node->decision;
        assert(sizeof(txmap_decision_node) == size);
}

bool TXMapSynchronizer::validate_txn(txmap_commit_node *commit_node) {
        uint32_t i, num_entry;
        txmap_record *record;
        txmap_set *rset, *wset;
        bool valid = true;
        uint64_t latest_key_version;

        rset = &commit_node->read_set;
        wset = &commit_node->write_set;
        assert(rset != NULL);
        assert(wset != NULL);

        std::stringstream ss;

        num_entry = rset->num_entry;
        for (i = 0; i < num_entry; i++) {
                record = &rset->set[i];         // FIXME: Wrong precedence?
                latest_key_version = get_latest_key_version(record->key);
                if (latest_key_version != record->version) {
                        valid = false;
                        break;
                }
        } 
        // DEBUG =======
        log(val_file, "VALIDATION", reinterpret_cast<txmap_node*>(commit_node), 0, latest_key_version, valid);

        return valid;
}

uint64_t TXMapSynchronizer::get_latest_key_version(uint64_t key) {
        return get(key);        // XXX Hack: let's store version into value
}

void TXMapSynchronizer::update_map(txmap_commit_node *commit_node, LocationInColor commit_version) {
        // DEBUG =========
        log(val_file, "APPLY", reinterpret_cast<txmap_node*>(commit_node), commit_version);

        uint32_t i, num_entry;
        txmap_set *wset; 
        wset = &commit_node->write_set;
        num_entry = wset->num_entry;
        for (i = 0; i < num_entry; i++) {
                txmap_record &r = wset->set[i];
                if (is_local_key(r.key))
                        put(r.key, commit_version);
        }
}

bool TXMapSynchronizer::is_local_key(uint64_t key) {
        TXMapContext *ctx = static_cast<TXMapContext*>(m_context);
        return key >= ctx->m_local_key_range_start && key < ctx->m_local_key_range_end;
}

bool TXMapSynchronizer::is_local_only_txn(txmap_commit_node* commit_node) {
        bool is_local = true;
        uint32_t i, num_entry;
        txmap_set *wset; 
        wset = &commit_node->write_set;
        num_entry = wset->num_entry;
        for (i = 0; i < num_entry; i++) {
                txmap_record &r = wset->set[i];
                if (!is_local_key(r.key)) {
                        is_local = false;
                        break;
                }
        }
        return is_local;
}

void TXMapSynchronizer::buffer_commit_node(txmap_commit_node* node, LocationInColor commit_version) {
        // DEBUG =========
        log(val_file, "BUFFER NODE", reinterpret_cast<txmap_node*>(node), commit_version);

        m_buffered_commit_nodes.push_back(node);
        m_buffered_commit_versions.push_back(commit_version);
}

void TXMapSynchronizer::buffer_decision_node(txmap_decision_node* node) {
        m_buffered_decision_nodes.push_back(node);
}

bool TXMapSynchronizer::apply_buffered_nodes(txmap_decision_node* decision_node) {
        assert(m_buffered_commit_nodes.size() > 0);
        assert(m_buffered_commit_nodes.size() == m_buffered_commit_versions.size());
        // DEBUG ========
        log(val_file, "DECISION NODE ARRIVED", reinterpret_cast<txmap_node*>(decision_node));

        txmap_commit_node* commit_node = m_buffered_commit_nodes.front();
        LocationInColor commit_version = m_buffered_commit_versions.front();
        if (commit_version != decision_node->commit_version) {
                // buffer decision node
                log(val_file, "DECISION NODE NOT MATCHING", reinterpret_cast<txmap_node*>(decision_node), commit_version);
                buffer_decision_node(decision_node->clone());
        } else {
                // apply 
                if (decision_node->decision == txmap_decision_node::DecisionType::COMMITTED) {
                        update_map(commit_node, decision_node->commit_version);
                        m_context->inc_num_committed();

                } else if (decision_node->decision == txmap_decision_node::DecisionType::ABORTED) {
                        m_context->inc_num_aborted();

                } else {
                        assert(false);
                }
                m_buffered_commit_nodes.pop_front();
                m_buffered_commit_versions.pop_front();
                delete commit_node;

                // Keep applying if possible
                while (m_buffered_commit_nodes.size() > 0) {

                        commit_node = m_buffered_commit_nodes.front();
                        commit_version = m_buffered_commit_versions.front();
                        if (is_decision_possible(commit_node)) {
                                // Validate
                                bool valid = validate_txn(commit_node);
                                if (valid) {
                                        update_map(commit_node, commit_version);
                                        m_context->inc_num_committed();

                                } else {
                                        m_context->inc_num_aborted();
                                }

                                // Append decision node to remote color if this is not local only commit
                                if (!is_local_only_txn(commit_node)) 
                                        append_decision_node_to_remote(commit_version, valid);
                                m_buffered_commit_nodes.pop_front();
                                m_buffered_commit_versions.pop_front();
                                delete commit_node;

                        } else {
                                bool decision_record_exists = false;
                                uint32_t i = 0;
                                for (auto d : m_buffered_decision_nodes) {
                                        if (commit_version == d->commit_version) {
                                                if (d->decision == txmap_decision_node::DecisionType::COMMITTED) { 
                                                        update_map(commit_node, d->commit_version);
                                                        m_context->inc_num_committed();

                                                } else if (d->decision == txmap_decision_node::DecisionType::ABORTED) {
                                                        m_context->inc_num_aborted();

                                                } else {
                                                        assert(false);
                                                }
                                                decision_record_exists = true; 
                                                break;
                                        }
                                        i++;
                                }
                                if (decision_record_exists) {
                                        // Remove decision record from this buffer
                                        m_buffered_decision_nodes.erase(m_buffered_decision_nodes.begin() + i); 
                                        m_buffered_commit_nodes.pop_front();
                                        m_buffered_commit_versions.pop_front();
                                        delete commit_node;
                                } else {
                                        break;
                                }
                        }
                }
        }
        assert(m_buffered_commit_nodes.size() == m_buffered_commit_versions.size());
        return m_buffered_commit_nodes.size() > 0;
}

void TXMapSynchronizer::log(char *file_name, char *prefix, txmap_node *node, LocationInColor commit_version, LocationInColor latest_key_version, bool decision) {
//      std::ofstream result_file; 
//      result_file.open(file_name, std::ios::app | std::ios::out);
//      result_file << "========== " << prefix << " ==========" << std::endl; 
//      if (node->node_type == txmap_node::NodeType::COMMIT_RECORD) {
//              txmap_commit_node* commit_node = reinterpret_cast<txmap_commit_node*>(node);
//              result_file << "[R] " << commit_node->read_set.log(); 
//              result_file << "[W] " << commit_node->write_set.log(); 
//              if (commit_version != 0) result_file << "[COMMIT_VER] " << commit_version << std::endl;
//              if (latest_key_version != 0) result_file << "[LATEST_VER] " << latest_key_version << ", DECISION:" << (decision ? "COMMIT" : "ABORT") << std::endl;
//              bool is_local_commit = is_local_key(commit_node->read_set.set[0].key);
//              result_file << (is_local_commit ? "[LOCAL COMMIT]" : "[REMOTE COMMIT]") << std::endl;

//      } else if (node->node_type == txmap_node::NodeType::DECISION_RECORD) {
//              txmap_decision_node* decision_node = reinterpret_cast<txmap_decision_node*>(node);
//              result_file << "[RD] " << decision_node->commit_version << "," << decision_node->decision << std::endl; 
//              if (commit_version != 0) result_file << "[BUFFERED_HEAD_COMMIT_VER] " << commit_version << std::endl;
//      }
//      result_file.close();        
}
