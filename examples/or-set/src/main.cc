#include <or_set.h>
#include <or_set_tester.h>
#include <chrono>
#include <workload_generator.h>
#include <config.h>
#include <tuple>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>
#include <iostream>
#include <fstream> 

void write_output(config cfg, const std::vector<double> &results, const std::vector<tester_request*> latencies)
{
	std::ofstream result_file;	
	result_file.open(std::to_string(cfg.server_id) + ".txt" , std::ios::trunc | std::ios::out);
	for (auto v : results) {
		result_file << v << "\n";
	}
	result_file.close();
	
	std::ofstream latency_file;
	latency_file.open(std::to_string(cfg.server_id) + "_latency.txt", std::ios::trunc | std::ios::out);
	for (auto rq : latencies) {
		if (rq->_executed == false)
			break;
		std::chrono::duration<double, std::milli> rq_duration = rq->_end_time - rq->_start_time;
		latency_file << rq_duration.count() << "\n"; 
	}
	latency_file.close();
}

void gen_input(uint64_t range, uint64_t num_inputs, std::vector<tester_request*> &output)
{
	workload_generator gen(range);
	uint64_t i;
	
	for (i = 0; i < num_inputs; ++i) {
		auto rq = static_cast<or_set_rq*>(malloc(sizeof(or_set_rq))); 
		rq->_key = gen.gen_next();
		rq->_opcode = or_set::log_opcode::ADD;
			
		auto temp = reinterpret_cast<tester_request*>(rq);
		temp->_executed = false;
		output.push_back(temp);
	} 
}

void wait_signal(config cfg)
{
	assert(false);
	
	/*
	char buffer[DELOS_MAX_DATA_SIZE];
	size_t buf_sz;
	struct colors c, depends, interested;	
	auto num_received = 0;

	ColorID sig_color[1];	
	sig_color[0] = 1;
	c.mycolors = sig_color;
	c.numcolors = 1;
	buf_sz = 1;
	
	interested = c;

	auto handle = new_dag_handle_for_single_server(cfg.log_addr.c_str(), &c);

	depends.mycolors = NULL;
	depends.numcolors = 0;
		
	append(handle, buffer, buf_sz, &c, &depends);
	assert(c.numcolors == 1 && c.mycolors[0] == 1);
	while (num_received < cfg.num_clients) {
		snapshot(handle);
		while (true) {
			get_next(handle, buffer, &buf_sz, &c);
			assert(c.numcolors == 0 || c.numcolors == 1);	
			if (c.numcolors == 1) {
				//assert(c.mycolors[0] == 1);
				num_received += 1;
				free(c.mycolors);
			} else {
				break;
			}
		}
	}	
	close_dag_handle(handle);	
	*/
}

void run_crdt(config cfg, std::vector<tester_request*> &inputs, std::vector<double> &throughput_samples)
{
	struct colors c;
	c.numcolors = 1;
	c.mycolors = new ColorID[1];
	c.mycolors[0] = cfg.server_id + 1; 
	
	
	size_t num_servers = cfg.log_addr.size();
	assert(num_servers > 0);

	const char *lock_server = cfg.log_addr[0].c_str();
	const char *server_ips[num_servers];
	auto i = 0;
	for (auto ip : cfg.log_addr) { 
		server_ips[i] = ip.c_str();
		i += 1;
	}

	auto handle = new_dag_handle(lock_server, num_servers, server_ips, &c);
	
	//auto handle = new_dag_handle_for_single_server(cfg.log_addr.c_str(), &c);
	auto orset = new or_set(handle, &c, cfg.server_id, cfg.sync_duration);	

	auto tester = new or_set_tester(cfg.window_sz, orset, handle);
	
	gen_input(cfg.expt_range, cfg.num_rqs, inputs); 
//	wait_signal(cfg);	
	std::cerr << "Worker " << (uint64_t)cfg.server_id << " initialized!\n";
	tester->do_run(inputs, throughput_samples, cfg.sample_interval, cfg.expt_duration);
	close_dag_handle(handle);
}

void do_experiment(config cfg)
{
	std::vector<tester_request*> inputs;
	std::vector<double> throughput_samples;
	run_crdt(cfg, inputs, throughput_samples);
	write_output(cfg, throughput_samples, inputs);
}

int main(int argc, char **argv) 
{
	std::vector<uint64_t> results;
	config_parser cfg_prser;			
	config cfg = cfg_prser.get_config(argc, argv);

	do_experiment(cfg);	
	return 0;
}
