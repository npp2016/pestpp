/*  
	� Copyright 2012, David Welter
	
	This file is part of PEST++.
   
	PEST++ is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	PEST++ is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with PEST++.  If not, see<http://www.gnu.org/licenses/>.
*/

#include "network_wrapper.h"
#include "RunManagerYAMR.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <list>
#include <iterator>
#include <cassert>
#include <cstring>
#include <map>
#include <deque>
#include <utility>
#include <algorithm>
#include "network_wrapper.h"
#include "network_package.h"
#include "Transformable.h"
#include "utilities.h"
#include "Serialization.h"


using namespace std;
using namespace pest_utils;


SlaveInfoRec::SlaveInfoRec(int _socket_fd)
{
	socket_fd = _socket_fd;
	run_id = UNKNOWN_ID;
	group_id = UNKNOWN_ID;
	state = SlaveInfoRec::State::NEW;
	work_dir = "";
	linpack_time = std::chrono::hours(-500);
	run_time = std::chrono::hours(-500);
	start_time = std::chrono::system_clock::now();
	last_ping_time = std::chrono::system_clock::now();
	ping = false;
	failed_pings = 0;
}

bool SlaveInfoRec::CompareTimes::operator() (const SlaveInfoRec &a, const SlaveInfoRec &b)
{
	bool ret = false;
	if (a.run_time > std::chrono::milliseconds(0) && b.run_time > std::chrono::milliseconds(0))
	{
		ret = (a.run_time < b.run_time);
	}
	else if (a.linpack_time > std::chrono::milliseconds(0) && b.linpack_time > std::chrono::milliseconds(0))
	{
		ret = (a.linpack_time < b.linpack_time);
	}
	return ret;
}


int SlaveInfoRec::get_socket_fd() const
{
	return socket_fd;
}

void SlaveInfoRec::set_socket_fd(int _socket_fd)
{
	socket_fd = _socket_fd;
}

int SlaveInfoRec::get_run_id() const
{
	return run_id;
}

void SlaveInfoRec::set_run_id(int _run_id)
{
	run_id = _run_id;
}

int SlaveInfoRec::get_group_id() const
{
	return group_id;
}

void SlaveInfoRec::set_group_id(int _group_id)
{
	group_id = _group_id;
}

SlaveInfoRec::State SlaveInfoRec::get_state() const
{
	return state;
}

void SlaveInfoRec::set_state(const State &_state)
{
	if (_state == SlaveInfoRec::State::ACTIVE)
	{
		throw PestError("SlaveInfo::set_state: run_id and group_id must be supplied when state it set to active");
	}
	state = _state;
}

void SlaveInfoRec::set_state(const State &_state, int _run_id, int _group_id)
{
	state = _state;
	run_id = _run_id;
	group_id = _group_id;
}

void SlaveInfoRec::set_work_dir(const std::string &_work_dir)
{
	work_dir = _work_dir;
}

string SlaveInfoRec::get_work_dir() const
{
	return work_dir;
}

void SlaveInfoRec::start_timer()
{
	start_time = std::chrono::system_clock::now();
}

void SlaveInfoRec::end_run()
{
	auto dt = std::chrono::system_clock::now() - start_time;
	if (run_time > std::chrono::hours(0))
	{
		run_time = run_time + dt;
		run_time /= 2;
	}
	else
	{
		run_time = dt;
	}
}

void SlaveInfoRec::end_linpack()
{
	linpack_time = std::chrono::system_clock::now() - start_time;
}

double SlaveInfoRec::get_duration_sec() const
{
	chrono::system_clock::duration dt = chrono::system_clock::now() - start_time;
	return (double)std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() / 1000.0;
}

double SlaveInfoRec::get_duration_minute() const
{
	return get_duration_sec() / 60.0;
}

double SlaveInfoRec::get_runtime_sec() const
{
	return(double)std::chrono::duration_cast<std::chrono::milliseconds>(run_time).count() / 1000.0;
}

double SlaveInfoRec::get_runtime_minute() const
{
	double run_minutes = std::chrono::duration_cast<std::chrono::milliseconds>(run_time).count() / 60000.0;
	return run_minutes;
}

double SlaveInfoRec::get_runtime() const
{
	return double(run_time.count());
}

double SlaveInfoRec::get_linpack_time() const
{
	return double(linpack_time.count());
}


void SlaveInfoRec::reset_failed_pings()
{
	failed_pings = 0;
}

int SlaveInfoRec::add_failed_ping()
{
	failed_pings++;
	return failed_pings;
}

void SlaveInfoRec::set_ping(bool val)
{
	ping = val;
	//a success response
	if (!val) reset_failed_pings();
	//sending a request
	else reset_last_ping_time();
}

bool SlaveInfoRec::get_ping() const
{
	return ping;
}

void SlaveInfoRec::reset_last_ping_time()
{
	last_ping_time = chrono::system_clock::now();
}

int SlaveInfoRec::seconds_since_last_ping_time() const
{
	return chrono::duration_cast<std::chrono::seconds>
		(chrono::system_clock::now() - last_ping_time).count();
}


RunManagerYAMR::RunManagerYAMR(const vector<string> _comline_vec,
	const vector<string> _tplfile_vec, const vector<string> _inpfile_vec,
	const vector<string> _insfile_vec, const vector<string> _outfile_vec,
	const string &stor_filename, const string &_port, ofstream &_f_rmr, int _max_n_failure)
	: RunManagerAbstract(_comline_vec, _tplfile_vec, _inpfile_vec,
	_insfile_vec, _outfile_vec, stor_filename, _max_n_failure),
	port(_port), f_rmr(_f_rmr)
{
	max_concurrent_runs = max(MAX_CONCURRENT_RUNS_LOWER_LIMIT, _max_n_failure);
	w_init();
	int status;
	struct addrinfo hints;
	struct addrinfo *servinfo;
	memset(&hints, 0, sizeof hints);
	//Use this for IPv4 aand IPv6
	//hints.ai_family = AF_UNSPEC;
	//Use this just for IPv4;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	status = w_getaddrinfo(NULL, port.c_str(), &hints, &servinfo);
	cout << "          starting YAMR (Yet Another run ManageR)..." << endl << endl;
	w_print_servinfo(servinfo, cout);
	cout << endl;
	//make socket, bind and listen
	addrinfo *connect_addr = w_bind_first_avl(servinfo, listener);
	if (connect_addr == nullptr)
	{
		stringstream err_str;
		err_str << "Error: port \"" << port << "\n is busy.  Can not bind port" << endl;
		throw(PestError(err_str.str()));
	}
	else {
		f_rmr << endl;
		cout << "YAMR Master listening on socket: " << w_get_addrinfo_string(connect_addr) << endl;
		f_rmr << "YAMR Master listening on socket:" << w_get_addrinfo_string(connect_addr) << endl;
	}
	w_listen(listener, BACKLOG);
	//free servinfo
	freeaddrinfo(servinfo);
	fdmax = listener;
	FD_ZERO(&master);
	FD_SET(listener, &master);	
	return;
}

int RunManagerYAMR::get_n_concurrent(int run_id)
{
	auto range_pair = active_runid_to_iterset_map.equal_range(run_id);
	int n = 0;
	for (auto &i = range_pair.first; i != range_pair.second; ++i)
	{
		++n;
	}
	return n;
}

list<SlaveInfoRec>::iterator RunManagerYAMR::get_active_run_iter(int socket)
{
	auto iter = socket_to_iter_map.find(socket);

	if (iter != socket_to_iter_map.end())
	{
		return socket_to_iter_map.find(socket)->second;
	}
	else
	{
		return slave_info_set.end();
	}
}


void RunManagerYAMR::initialize(const Parameters &model_pars, const Observations &obs, const string &_filename)
{
	RunManagerAbstract::initialize(model_pars, obs, _filename);
	cur_group_id = NetPackage::get_new_group_id();
}

void RunManagerYAMR::initialize_restart(const std::string &_filename)
{
	file_stor.init_restart(_filename);
	free_memory();
	vector<int> waiting_run_id_vec = get_outstanding_run_ids();
	for (int &id : waiting_run_id_vec)
	{
		waiting_runs.push_back(id);
	}
}

void RunManagerYAMR::reinitialize(const std::string &_filename)
{
	free_memory();
	RunManagerAbstract::reinitialize(_filename);
	cur_group_id = NetPackage::get_new_group_id();
}

void  RunManagerYAMR::free_memory()
{
	waiting_runs.clear();
	model_runs_done = 0;
	failure_map.clear();
	active_runid_to_iterset_map.clear();
}

int RunManagerYAMR::add_run(const Parameters &model_pars, const string &info_txt, double info_value)
{
	int run_id = file_stor.add_run(model_pars, info_txt, info_value);
	waiting_runs.push_back(run_id);
	return run_id;
}

int RunManagerYAMR::add_run(const std::vector<double> &model_pars, const string &info_txt, double info_value)
{
	int run_id = file_stor.add_run(model_pars, info_txt, info_value);
	waiting_runs.push_back(run_id);
	return run_id;
}

int RunManagerYAMR::add_run(const Eigen::VectorXd &model_pars, const string &info_txt, double info_value)
{
	int run_id = file_stor.add_run(model_pars, info_txt, info_value);
	waiting_runs.push_back(run_id);
	return run_id;
}

void RunManagerYAMR::update_run(int run_id, const Parameters &pars, const Observations &obs)
{

	file_stor.update_run(run_id, pars, obs);
	// erase any wating runs with this id
	for (auto it_run = waiting_runs.begin(); it_run != waiting_runs.end();)
	{
		if (*it_run == run_id)
		{
			it_run = waiting_runs.erase(it_run);
		}
		else
		{
			++it_run;
		}
	}
	// kill any active runs with this id
	kill_runs(run_id);
}

void RunManagerYAMR::run()
{
	stringstream message;
	NetPackage net_pack;
	model_runs_done = 0;
	model_runs_failed = 0;
	failure_map.clear();
	active_runid_to_iterset_map.clear();
	cout << "    running model " << waiting_runs.size() << " times" << endl;
	f_rmr << "running model " << waiting_runs.size() << " times" << endl;
	if (slave_info_set.size() == 0) // first entry is the listener, slave apears after this
	{
		cout << endl << "      waiting for slaves to appear..." << endl << endl;
		f_rmr << endl << "    waiting for slaves to appear..." << endl << endl;
	}
	cout << endl;
	f_rmr << endl;
	while (!all_runs_complete())
	{
		echo();
		init_slaves();
		//schedule runs on available nodes
		schedule_runs();
		// get and process incomming messages
		listen();		
	}
	echo();
	total_runs += model_runs_done;
	//kill any remaining active runs
	kill_all_active_runs();
	echo();
	message.str("");
	message << "    " << model_runs_done << " runs complete";
	cout << endl << "---------------------" << endl << message.str() << endl << endl;
	f_rmr << endl << "---------------------" << endl << message.str() << endl << endl;
	
	if (init_sim.size() == 0)
	{
		vector<double> pars;
		int status = file_stor.get_run(0, pars, init_sim);
	}
}


void RunManagerYAMR::listen()
{
	struct sockaddr_storage remote_addr;
	fd_set read_fds; // temp file descriptor list for select()
	socklen_t addr_len;
	timeval tv;
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	read_fds = master; // copy it
	if (w_select(fdmax+1, &read_fds, NULL, NULL, &tv) == -1) 
	{
		return;
	}
	// run through the existing connections looking for data to read
	for(int i = 0; i <= fdmax; i++) {
		if (FD_ISSET(i, &read_fds)) { // we got one!!
			if (i == listener)  // handle new connections
			{
				int newfd;
				addr_len = sizeof remote_addr;
				newfd = w_accept(listener,(struct sockaddr *)&remote_addr, &addr_len);
				if (newfd == -1) {}
				else 
				{
					FD_SET(newfd, &master); // add to master set
					if (newfd > fdmax) { // keep track of the max
						fdmax = newfd;
					}
					vector<string> sock_name = w_getnameinfo_vec(newfd);
					add_slave(newfd);
				}
			}
			else  // handle data from a client
			{				
				//set the ping flag since the slave sent something back
				list<SlaveInfoRec>::iterator iter = socket_to_iter_map.at(i);
				iter->set_ping(false);
				process_message(i);				
			} // END handle data from client
		} // END got new incoming connection
		else
		{		
			map<int, list<SlaveInfoRec>::iterator>::iterator iter = socket_to_iter_map.find(i);
			if (iter != socket_to_iter_map.end())
			{
				SlaveInfoRec::State state = iter->second->get_state();
				if (state == SlaveInfoRec::State::WAITING
					|| state == SlaveInfoRec::State::ACTIVE
					|| state == SlaveInfoRec::State::COMPLETE
					|| state == SlaveInfoRec::State::KILLED
					|| state == SlaveInfoRec::State::KILLED_FAILED)
				{
					iter->second->set_ping(false);
					break;
				}
			}
		}			
	} // END looping through file descriptors
}

void RunManagerYAMR::ping(int i_sock)
{				
	list<SlaveInfoRec>::iterator slave_info_iter = socket_to_iter_map.at(i_sock);
	vector<string> sock_name = w_getnameinfo_vec(i_sock);
	fd_set read_fds = master;
	//if the slave hasn't communicated since the last ping request
	if ((!FD_ISSET(i_sock, &read_fds)) && slave_info_iter->get_ping())
	{
		int fails = slave_info_iter->add_failed_ping();
		report("failed to receive ping response from slave: " + sock_name[0] + "$" + slave_info_iter->get_work_dir(), false);
		if (fails >= MAX_FAILED_PINGS)
		{
			report("max failed ping communications since last successful run for slave:" + sock_name[0] + "$" + slave_info_iter->get_work_dir() + "  -> terminating", false);
			close_slave(i_sock);
			return;
		}		
	}
	//check if it is time to ping again...
	double duration = (double)slave_info_iter->seconds_since_last_ping_time();
	double ping_time = max(double(PING_INTERVAL_SECS), slave_info_iter->get_runtime_sec());
	if (duration >= ping_time)
	{		
		const char* data = "\0";
		NetPackage net_pack(NetPackage::PackType::PING, 0, 0, "");
		int err = net_pack.send(i_sock, data, 0);
		if (err <= 0)
		{
			int fails = slave_info_iter->add_failed_ping();
			report("failed to send ping request to slave:" + sock_name[0] + "$" + slave_info_iter->get_work_dir(), false);
			if (fails >= MAX_FAILED_PINGS)
			{
				report("max failed ping communications since last successful run for slave:" + sock_name[0] + "$" + slave_info_iter->get_work_dir() + "  -> terminating", true);
				close_slave(i_sock);
				return;
			}
		}
		else slave_info_iter->set_ping(true);
#ifdef _DEBUG
		//report("ping sent to slave:" + sock_name[0] + "$" + slave_info.get_work_dir(i_sock), false);
#endif
	}
}


void RunManagerYAMR::close_slave(int i_sock)
{
	list<SlaveInfoRec>::iterator slave_info_iter = socket_to_iter_map.at(i_sock);
	close_slave(slave_info_iter);
}

void RunManagerYAMR::close_slave(list<SlaveInfoRec>::iterator slave_info_iter)
{
	int i_sock = slave_info_iter->get_socket_fd();
	vector<string> sock_name = w_getnameinfo_vec(i_sock);
	w_close(i_sock); // bye!
	FD_CLR(i_sock, &master); // remove from master set
	//remove slave from socket_to_iter_map 
	socket_to_iter_map.erase(i_sock);
	// remove run from active_runid_to_iterset_map
	for (auto iter = active_runid_to_iterset_map.begin(), ite = active_runid_to_iterset_map.end(); iter != ite;)
	{
		if (iter->second == slave_info_iter)
		{
			int run_id = iter->second->get_run_id();
			if (!run_finished(run_id)) //check if run has already finish on another node
			{
				waiting_runs.push_front(run_id);
			}
			iter = active_runid_to_iterset_map.erase(iter);
			break;
		}
		else
			++iter;
	}
	slave_info_set.erase(slave_info_iter);
	socket_to_iter_map.erase(i_sock);
	stringstream ss;
	
	ss << "closed connection to slave: " << sock_name[0] << ":" << sock_name[1] << "; number of slaves: " << socket_to_iter_map.size();
	report(ss.str(), false);
}


void RunManagerYAMR::schedule_runs()
{
	NetPackage net_pack;

	std::list<list<SlaveInfoRec>::iterator> free_slave_list = get_free_slave_list();
	//first try to schedule waiting runs
	for (auto it_run = waiting_runs.begin(); !free_slave_list.empty() && it_run != waiting_runs.end();)
	{
		int success = schedule_run(*it_run, free_slave_list);
		if (success >= 0)
		{
			it_run = waiting_runs.erase(it_run);
		}
		else
		{
			++it_run;
		}
	}

	//check for overdue runs
	try
	{
		double duration, avg_runtime;
		double global_avg_runtime = get_global_runtime_minute();
		bool should_schedule = false;

		for (auto it_active = active_runid_to_iterset_map.begin(); it_active != active_runid_to_iterset_map.end(); ++it_active)
		{
			should_schedule = false;
			auto it_slave = it_active->second;
			int run_id = it_slave->get_run_id();
			int act_sock_id = it_slave->get_socket_fd();
			int n_concur = get_n_concurrent(run_id);

			duration = it_slave->get_duration_minute();
			avg_runtime = it_slave->get_runtime_minute();
			if (avg_runtime <= 0) avg_runtime = global_avg_runtime;
			if (avg_runtime <= 0) avg_runtime = 1.0E+10;
			vector<int> overdue_kill_runs_vec = get_overdue_runs_over_kill_threshold(run_id);
			if (it_slave->get_state() == SlaveInfoRec::State::KILLED)
			{
				//this run has already been killed.  No need to rekill it
				should_schedule = false;
			}
			else if (failure_map.count(run_id) + overdue_kill_runs_vec.size() >= max_n_failure)
			{
				// kill the overdue runs
				kill_runs(run_id);
				should_schedule = false;
			}
			else if (overdue_kill_runs_vec.size() >= max_concurrent_runs)
			{
				// kill the overdue runs
				kill_runs(run_id);
				// reschedule runs as we still haven't reach the max failure threshold
				// and there are not concurrent runs for this id becuse we just killed all of them
				should_schedule = true;
			}
			else if (duration > avg_runtime*PERCENT_OVERDUE_RESCHED)
			{
				//check how many concurrent runs are going	
				if (n_concur < max_concurrent_runs) should_schedule = true;
				else should_schedule = false;
			}

			if ((!free_slave_list.empty()) && (should_schedule))
			{
				vector<string> sock_name = w_getnameinfo_vec(act_sock_id);
				stringstream ss;
				ss << "rescheduling overdue run " << run_id << " (" << duration << "|" <<
					avg_runtime << " minutes) on: " << sock_name[0] << "$" <<
					it_slave->get_work_dir();
				report(ss.str(), false);
				int success = schedule_run(run_id, free_slave_list);
				if (success >=0)
				{
					stringstream ss;
					ss << n_concur << " concurrent runs for run id = " << run_id;
					report(ss.str(), false);
				}
				else
				{
					stringstream ss;
					ss << "failed to schedule concurrent run for run id = " << run_id;
					report(ss.str(), false);
				}
			}
		}
	}
	catch (exception &e)
	{
		cout << "exception trying to find overdue runs: " << endl << e.what() << endl;
	}
}

int RunManagerYAMR::schedule_run(int run_id, std::list<list<SlaveInfoRec>::iterator> &free_slave_list)
{
	int scheduled = -1;
	auto it_slave = free_slave_list.end(); // iterator to current socket

	if (run_finished(run_id))
	{
		// run already completed on different node.  Do nothing
		scheduled = 0;
	}
	else if (failure_map.count(run_id) >= max_n_failure)
	{
		//if this run has already failed the max number of times, do nothing
		scheduled = 0;
	}
	else if (failure_map.count(run_id) == 0)// || failure_map.count(run_id) >= slave_fd.size())
	{
		// schedule a run on a slave
		it_slave = free_slave_list.begin();
		scheduled = -1;
	}
	else if (failure_map.count(run_id) >= slave_info_set.size())
	{
		// enough enough slaves to make all failed runs on different slaves
		// schedule a run on a slave
		it_slave = free_slave_list.begin();
		scheduled = -1;
	}
	else if (failure_map.count(run_id) > 0)
	{
		for (it_slave = free_slave_list.begin(); it_slave != free_slave_list.end(); ++it_slave)
		{
			int socket_fd = (*it_slave)->get_socket_fd();
			auto fail_iter_pair = failure_map.equal_range(run_id);

			auto i = fail_iter_pair.first;
			for (i = fail_iter_pair.first;
				i != fail_iter_pair.second && i->second != socket_fd;
				++i) {
			}
			if (i == fail_iter_pair.second)  // This is slave has not previously failed on this run
			{
				// This run has not previously failed on this slave
				// Schedule run on it_sock
				break;
			}
		}
	}
	if (it_slave != free_slave_list.end())
	{
		int socket_fd = (*it_slave)->get_socket_fd();
		vector<char> data = file_stor.get_serial_pars(run_id);
		vector<string> sock_name = w_getnameinfo_vec(socket_fd);
		NetPackage net_pack(NetPackage::PackType::START_RUN, cur_group_id, run_id, "");
		int err = net_pack.send(socket_fd, &data[0], data.size());
		if (err != -1)
		{
			(*it_slave)->set_state(SlaveInfoRec::State::ACTIVE, run_id, cur_group_id);
			//start run timer
			(*it_slave)->start_timer();
			//reset the last ping time so we don't ping immediately after run is started
			(*it_slave)->reset_last_ping_time();
			active_runid_to_iterset_map.insert(make_pair(run_id, *it_slave));
			stringstream ss;
			ss << "Sending run " << run_id << " to: " << sock_name[0] << "$" << (*it_slave)->get_work_dir() <<
				"  (group id = " << cur_group_id << ", run id = " << run_id << ", concurrent runs = " << get_n_concurrent(run_id) << ")";
			report(ss.str(), false);
			free_slave_list.erase(it_slave);
			scheduled = 1;			
		}
	}
	return scheduled;  // 1 = run scheduled; -1 failed to schedule run; 0 run not needed
}



void RunManagerYAMR::echo()
{
	cout << get_time_string() << "->" << setw(6) << model_runs_done << " runs complete " <<
		setw(6) << model_runs_failed << " runs failed " <<
		setw(4) << slave_info_set.size() << " slaves\r" << flush;

}

string RunManagerYAMR::get_time_string()
{
	std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	string t_str = ctime(&tt);
	return t_str.substr(0, t_str.length() - 1);

}

void RunManagerYAMR::report(std::string message,bool to_cout)
{
	string t_str = get_time_string();
	f_rmr << t_str << "->" << message << endl;
	if (to_cout) cout << endl << t_str << "->" << message << endl;
}

void RunManagerYAMR::process_message(int i_sock)
{	
	echo();
	NetPackage net_pack;
	int err;
	list<SlaveInfoRec>::iterator slave_info_iter = socket_to_iter_map.at(i_sock);

	vector<string> sock_name = w_getnameinfo_vec(i_sock);

	if(( err=net_pack.recv(i_sock)) <=0) // error or lost connection
	{
		if (err < 0) {
			report("receive failed from slave: " + sock_name[0] + "$" + slave_info_iter->get_work_dir() + " - terminating slave", false);
		}
		else {
			report("lost connection to slave: " + sock_name[0] + "$" + slave_info_iter->get_work_dir(), false);
		}
		close_slave(i_sock);
	}
	else if (net_pack.get_type() == NetPackage::PackType::RUNDIR)
	{
		string work_dir(net_pack.get_data().data(), net_pack.get_data().size());
		stringstream ss;
		ss << "initializing new slave connection from: " << sock_name[0] << ":" << sock_name[1] << "; number of slaves: " << socket_to_iter_map.size() << "; working dir: " << work_dir;
		report(ss.str(),false);		
		slave_info_iter->set_work_dir(work_dir);
		slave_info_iter->set_state(SlaveInfoRec::State::CWD_RCV);
	}
	else if (net_pack.get_type() == NetPackage::PackType::LINPACK)
	{
		slave_info_iter->end_linpack();
		slave_info_iter->set_state(SlaveInfoRec::State::LINPACK_RCV);
		stringstream ss;
		ss << "new slave ready: " << sock_name[0] << ":" << sock_name[1];
		report(ss.str(), false);
	}
	else if (net_pack.get_type() == NetPackage::PackType::READY)
	{
		// ready message received from slave
		slave_info_iter->set_state(SlaveInfoRec::State::WAITING);
	}

	else if ( (net_pack.get_type() == NetPackage::PackType::RUN_FINISHED 
		|| net_pack.get_type() == NetPackage::PackType::RUN_FAILED
		|| net_pack.get_type() == NetPackage::PackType::RUN_KILLED)
			&& net_pack.get_group_id() != cur_group_id)
	{		
		// this is an old run that did not finish on time
		// just ignore it
		int run_id = net_pack.get_run_id();
		int group_id = net_pack.get_group_id();
		//stringstream ss;
		//ss << "run " << run_id << " received from unexpected group id: " << group_id << ", should be group: " << cur_group_id;
		//throw PestError(ss.str());
	}
	else if (net_pack.get_type() == NetPackage::PackType::RUN_FINISHED)
	{		
		int run_id = net_pack.get_run_id();
		int group_id = net_pack.get_group_id();
	
		//check if this run already completed on another node
		int status;
		string info_text;
		double info_value;
		get_info(run_id, status, info_text, info_value);
		if (status > 0)
		{
			stringstream ss;
			ss << "Prevoiusly completed run: " << run_id << " finished on: " << sock_name[0] << "$" << slave_info_iter->get_work_dir() <<
				"  (run time = " << slave_info_iter->get_runtime_minute() << " min, group id = " << group_id <<
				", run id = " << run_id << " concurrent = " << get_n_concurrent(run_id) << ")";
			report(ss.str(), false);
		}
		else
		{
			// keep track of model run time
			slave_info_iter->end_run();
			stringstream ss;
			ss << "run " << run_id << " received from: " << sock_name[0] << "$" << slave_info_iter->get_work_dir() <<
				"  (run time = " << slave_info_iter->get_runtime_minute() << " min, group id = " << group_id <<
				", run id = " << run_id << " concurrent = " << get_n_concurrent(run_id) << ")";
			report(ss.str(), false);
			process_model_run(i_sock, net_pack);
		}



	}
	else if (net_pack.get_type() == NetPackage::PackType::RUN_FAILED)
	{
		int run_id = net_pack.get_run_id();
		int group_id = net_pack.get_group_id();
		int n_concur = get_n_concurrent(run_id);
		stringstream ss;
		ss << "Run " << run_id << " failed on slave:" << sock_name[0] << "$" << slave_info_iter->get_work_dir() << "  (group id = " << group_id << ", run id = " << run_id << ", concurrent = " << n_concur << ") ";
		report(ss.str(), false);
		model_runs_failed++;
		file_stor.update_run_failed(run_id);
		failure_map.insert(make_pair(run_id, i_sock));
		auto it = get_active_run_iter(i_sock);
		remove_from_active_runid_to_iterset_map(it);
		// TO DO add check for number of active nodes
		if ((n_concur == 0) && (failure_map.count(run_id) < max_n_failure))
		{
			//put model run back into the waiting queue
			waiting_runs.push_front(run_id);
		}
	}
	else if (net_pack.get_type() == NetPackage::PackType::RUN_KILLED)
	{
		int run_id = net_pack.get_run_id();
		int group_id = net_pack.get_group_id();
		int n_concur = get_n_concurrent(run_id);
		auto it = get_active_run_iter(i_sock);
		remove_from_active_runid_to_iterset_map(it);
		stringstream ss;
		ss << "Run " << run_id << " killed on slave: " << sock_name[0] << "$" << slave_info_iter->get_work_dir() << ", run id = " << run_id << " concurrent = " << n_concur;
		report(ss.str(), false);
		// If the run has not been completed on another node, count this as a failure
		if (!run_finished(run_id))
		{
			failure_map.insert(make_pair(run_id, i_sock));
			model_runs_failed++;
		}
	}
	else if (net_pack.get_type() == NetPackage::PackType::PING)
	{
#ifdef _DEBUG
		//report("ping received from slave" + sock_name[0] + "$" + slave_info.get_work_dir(i_sock),false);
#endif
	}
	else if (net_pack.get_type() == NetPackage::PackType::IO_ERROR)
	{
		//string err(net_pack.get_data().begin(),net_pack.get_data().end());		
		report("error in model IO files on slave: " + sock_name[0] + "$" + slave_info_iter->get_work_dir() + "-terminating slave. ", true);
		close_slave(i_sock);
	}
	else
	{
		report("received unsupported message from slave: ", false);
		net_pack.print_header(f_rmr);
		//save results from model run
	}
}

bool RunManagerYAMR::process_model_run(int sock_id, NetPackage &net_pack)
{
	list<SlaveInfoRec>::iterator slave_info_iter = socket_to_iter_map.at(sock_id);
	bool use_run = false;
	int run_id = net_pack.get_run_id();

	//check if another instance of this model run has already completed 
	if (!run_finished(run_id))
	{
		Parameters pars;
		Observations obs;
		Serialization::unserialize(net_pack.get_data(), pars, get_par_name_vec(), obs, get_obs_name_vec());
		file_stor.update_run(run_id, pars, obs);
		slave_info_iter->set_state(SlaveInfoRec::State::COMPLETE);
		use_run = true;
		model_runs_done++;
		
	}
	// remove currently completed run from the active list
	auto it = get_active_run_iter(sock_id);
	remove_from_active_runid_to_iterset_map(it);
	kill_runs(run_id);
	return use_run;
}

void RunManagerYAMR::kill_runs(int run_id)
{
	auto range_pair = active_runid_to_iterset_map.equal_range(run_id);
	//runs with this id are not needed so mark them as zombies
	for (auto b = range_pair.first; b != range_pair.second; ++b)
	{
		list<SlaveInfoRec>::iterator slave_info_iter = (*b).second;
		int socket_id = slave_info_iter->get_socket_fd();
		if (socket_id && slave_info_iter->get_state() != SlaveInfoRec::State::KILLED)
		{
			slave_info_iter->set_state(SlaveInfoRec::State::KILLED);
			//schedule run to be killed
			vector<string> sock_name = w_getnameinfo_vec(socket_id);
			stringstream ss;
			ss << "sending kill request for run " << run_id << " to slave : " << sock_name[0] << "$" << slave_info_iter->get_work_dir();
			report(ss.str(), false);
			NetPackage net_pack(NetPackage::PackType::REQ_KILL, 0, 0, "");
			char data = '\0';
			int err = net_pack.send(socket_id, &data, sizeof(data));
			if (err == 1)
			{
				slave_info_iter->set_state(SlaveInfoRec::State::KILLED);
			}
			else
			{
				report("error sending kill request to slave:" + sock_name[0] + "$" +
					slave_info_iter->get_work_dir(), true);
				slave_info_iter->set_state(SlaveInfoRec::State::KILLED_FAILED);
			}
		}
	}
}


void RunManagerYAMR::kill_all_active_runs()
{
	for (int n_tries = 0; !active_runid_to_iterset_map.empty() && n_tries >= 100; ++n_tries)
	{
		init_slaves();
		for (auto it_active = active_runid_to_iterset_map.begin(); it_active != active_runid_to_iterset_map.end(); ++it_active)
		{
			auto slave_iter = (*it_active).second;
			int run_id = slave_iter->get_run_id();
			int socket_id = slave_iter->get_socket_fd();
			SlaveInfoRec::State state = slave_iter->get_state();
			string work_dir = slave_iter->get_work_dir();
			if (socket_id && (state == SlaveInfoRec::State::ACTIVE || state == SlaveInfoRec::State::KILLED_FAILED))
			{
				//schedule run to be killed
				vector<string> sock_name = w_getnameinfo_vec(socket_id);
				stringstream ss;
				ss << "sending kill request for run " << run_id << " to slave : " << sock_name[0] << "$" << work_dir;
				report(ss.str(), false);
				NetPackage net_pack(NetPackage::PackType::REQ_KILL, 0, 0, "");
				char data = '\0';
				int err = net_pack.send(socket_id, &data, sizeof(data));
				if (err == 1)
				{
					slave_iter->set_state(SlaveInfoRec::State::KILLED);
				}
				else
				{
					report("error sending kill request to slave:" + sock_name[0] + "$" +
						work_dir, true);
					slave_iter->set_state(SlaveInfoRec::State::KILLED_FAILED);
				}
			}
		}
		listen();
	}
}

 void RunManagerYAMR::init_slaves()
 {
	 for (auto &i_slv : slave_info_set)
	 {
		int i_sock = i_slv.get_socket_fd();
		SlaveInfoRec::State cur_state = i_slv.get_state();
		if (cur_state == SlaveInfoRec::State::NEW)
		{
			NetPackage net_pack(NetPackage::PackType::REQ_RUNDIR, 0, 0, "");
			char data = '\0';
			int err = net_pack.send(i_sock, &data, sizeof(data));
			if (err != -1)
			{
				i_slv.set_state(SlaveInfoRec::State::CWD_REQ);
			}
		}
		else if (cur_state == SlaveInfoRec::State::CWD_RCV)
		{
			// send Command line, tpl and ins information
			NetPackage net_pack(NetPackage::PackType::CMD, 0, 0, "");
			vector<char> data;
			vector<vector<string> const*> tmp_vec;
			tmp_vec.push_back(&comline_vec);
			tmp_vec.push_back(&tplfile_vec);
			tmp_vec.push_back(&inpfile_vec);
			tmp_vec.push_back(&insfile_vec);
			tmp_vec.push_back(&outfile_vec);
			tmp_vec.push_back(&file_stor.get_par_name_vec());
			tmp_vec.push_back(&file_stor.get_obs_name_vec());

			data = Serialization::serialize(tmp_vec);
			int err = net_pack.send(i_sock, &data[0], data.size());
			if (err != -1)
			{
				i_slv.set_state(SlaveInfoRec::State::CMD_SENT);
			}
		}
		else if (cur_state == SlaveInfoRec::State::CMD_SENT)
		{
			NetPackage net_pack(NetPackage::PackType::REQ_LINPACK, 0, 0, "");
			char data = '\0';
			int err = net_pack.send(i_sock, &data, sizeof(data));
			if (err != -1)
			{
				i_slv.set_state(SlaveInfoRec::State::LINPACK_REQ);
				i_slv.start_timer();
			}
		}
		else if (cur_state == SlaveInfoRec::State::LINPACK_RCV)
		{
			i_slv.set_state(SlaveInfoRec::State::WAITING);
		}
	}
 }

 vector<int> RunManagerYAMR::get_overdue_runs_over_kill_threshold(int run_id)
 {
	 vector<int> sock_id_vec;
	 auto range_pair = active_runid_to_iterset_map.equal_range(run_id);
	
	 double duration;
	 for (auto &i = range_pair.first; i != range_pair.second; ++i)
	 {
		 if (i->second->get_state() == SlaveInfoRec::State::ACTIVE)
		 {
			 double avg_runtime = i->second->get_runtime_minute();
			 if (avg_runtime <= 0) avg_runtime = get_global_runtime_minute();;
			 if (avg_runtime <= 0) avg_runtime = 1.0E+10;
			 duration = i->second->get_duration_minute();
			 if (duration >= avg_runtime*PERCENT_OVERDUE_GIVEUP)
			 {
				 sock_id_vec.push_back(i->second->get_socket_fd());
			 }
		 }
	 }
	 return sock_id_vec;
 }

 bool RunManagerYAMR::all_runs_complete()
 {
	 // check for run in the waitng queue
	 if (!waiting_runs.empty())
	 {
		 return false;
	 }
	 // check for active runs
	 int socket_fd;
	 for (auto it_active = active_runid_to_iterset_map.begin(); it_active != active_runid_to_iterset_map.end(); ++it_active)
	 {
		 if (it_active->second->get_state() == SlaveInfoRec::State::ACTIVE)
		 {
			 return false;
		 }
	 }
	 return true;
 }


 list<SlaveInfoRec>::iterator RunManagerYAMR::add_slave(int sock_id)
 {
	 FD_SET(sock_id, &master); // add to master set

	 //list<SlaveInfoRec>::iterator
	slave_info_set.push_back(SlaveInfoRec(sock_id));
	list<SlaveInfoRec>::iterator iter = std::prev(slave_info_set.end());
	socket_to_iter_map[sock_id] = iter;
	return iter;
 }

 double RunManagerYAMR::get_global_runtime_minute() const
 {
	 double global_runtime = 0;
	 double temp = 0;
	 int count = 0;
	 for (auto &si : slave_info_set)
	 {
		 temp = si.get_runtime_minute();
		 if (temp > 0)
		 {
			 count++;
			 global_runtime += temp;
		 }
	 }
	 if (count == 0)
		 return 0.0;
	 return global_runtime / (double)count;
 }

 void RunManagerYAMR::remove_from_active_runid_to_iterset_map(list<SlaveInfoRec>::iterator slave_info_iter)
 {
	 int run_id = slave_info_iter->get_run_id();
	 // for (auto &i : active_runid_to_iterset_map)
	 //{
	//	 cerr << i.first << "   " << i.second->get_socket_fd() << endl;
	 //}
	 auto range_pair = active_runid_to_iterset_map.equal_range(run_id);


	 for (auto iter = range_pair.first; iter != range_pair.second;)
	 { 
		 if (iter->second == slave_info_iter)
		 {
			 iter = active_runid_to_iterset_map.erase(iter);
			 return;
		 }
		 else
		 {
			 ++iter;
		 }
	 }


	 // This code should never be called.  The previous loop should catch everything
	 assert(false);
	 for (auto iter = active_runid_to_iterset_map.begin(), ite = active_runid_to_iterset_map.end(); iter != ite;)
	 {
		 if (iter->second == slave_info_iter)
		 {
			 iter = active_runid_to_iterset_map.erase(iter);
			 break;
		 }
		 else
			 ++iter;
	 }
 }
 
 list<list<SlaveInfoRec>::iterator> RunManagerYAMR::get_free_slave_list()
 {
	 list<list<SlaveInfoRec>::iterator> iter_list;
	 list<SlaveInfoRec>::iterator iter_b, iter_e;
	 for (iter_b = slave_info_set.begin(), iter_e = slave_info_set.end();
		 iter_b != iter_e; ++iter_b)
	 {
		 SlaveInfoRec::State cur_state = iter_b->get_state();
		 if (cur_state == SlaveInfoRec::State::WAITING)
		 {
			 iter_list.push_back(iter_b);
		 }
	 }
	 return iter_list;
 }

RunManagerYAMR::~RunManagerYAMR(void)
{
	//close sockets and cleanup
	int err;
	err = w_close(listener);
	FD_CLR(listener, &master);
	// this is needed to ensure that the first slave closes properly
	w_sleep(2000);	
	for(int i = 0; i <= fdmax; i++) {
		if (FD_ISSET(i, &master)) 
		{
			NetPackage netpack(NetPackage::PackType::TERMINATE, 0, 0,"");
			char data;
			netpack.send(i, &data, 0);
			err = w_close(i);
			FD_CLR(i, &master);
		}
	}
	w_cleanup();
}
