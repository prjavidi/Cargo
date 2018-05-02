#include <iostream>
#include <vector>
#include <map>
#include <limits>
#include <algorithm>
#include <thread>
#include <mutex>
#include <mqueue.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
// #include "common.h"
// #include "gtree/gtree.h"
#include "NearestNeighbor.h"
// #include "libcargo/Solution.h"
// #include "libcargo.h"

// Input: set of customers R
// Output: set of assignments A

typedef std::vector<int> RequestBatch;
typedef std::vector<int> Assignments;

std::mutex g_mutex_veh;

auto selector = [](std::pair<VehicleId, Vehicle> pair) { return pair.second.route[pair.second.lv_node]; };
auto selector2 = [](std::pair<VehicleId, Vehicle> pair) { return pair.first; };

NearestNeighbor::NearestNeighbor(Simulator *sim) : Solution(sim)
{
    // temporarily hard-coded
    GTree::load("../data/roadnetworks/cd1.gtree");
    gtree_ = GTree::get();
}

void NearestNeighbor::VehicleOnline(const Trip &vehicle)
{
    // routes_[vehicle.id] = std::vector<NodeId> {vehicle.oid, vehicle.did};
    Trip replay;
    while (replay_queue_.try_dequeue(replay))
        request_queue_.enqueue(replay);
}

void NearestNeighbor::RequestOnline(const Trip &request)
{
    request_queue_.enqueue(request);
    Trip replay;
    while (replay_queue_.try_dequeue(replay))
        request_queue_.enqueue(replay);
}

void NearestNeighbor::UpdateVehicle(const Vehicle &vehicle, const SimTime time)
{
    // this function should be considered as "maintenance"
    // just get a copy of vehicle
    // make_pair makes a copy
    std::lock_guard<std::mutex> guard(g_mutex_veh);
    vehicles_[vehicle.id] = vehicle;
    // auto iter = vehicles_.find(vehicle.id);
    // if (iter == vehicles_.end())
    //     vehicles_.insert(std::make_pair(vehicle.id, vehicle));
    // else
    // {
    // vehicles_.at(vehicle.id) = vehicle;
    // }
    updates_[vehicle.id] = time;
    // std::cout << "veh " << vehicle.id << " received" << std::endl;
}

void NearestNeighbor::Receive()
{
    mq_unlink("/update_vehicle");
    mqd_t mq = mq_open("/update_vehicle", O_CREAT | O_RDWR, 0655, mq_attr{0, 1000000, 1024, 0});
    SimTime st;
    Vehicle v;
    while (!done_)
    {
        int err = mq_receive(mq, (char *)(&st), 1024, NULL);
        if (err == -1)
        {
            std::cout << strerror(errno) << std::endl;
            done_ = true;
        }
        mq_receive(mq, (char *)(&v), 1024, NULL);
        std::cout << "veq " << v.id << " received" << std::endl;
    }
}

// Run function, should contain a loop
// If you want to batch requests, you are on your own
void NearestNeighbor::Run()
{
    std::cout << "running" << std::endl;
    while (!done_)
    {
        Trip request;
        bool succeed = request_queue_.try_dequeue(request);
        if (succeed)
        {
            const int increment = 10;
            // lock the map for retrieve
            g_mutex_veh.lock();

            const int n = vehicles_.size();
            std::vector<int> locations(n);
            std::vector<int> indexes(n);
            std::transform(vehicles_.begin(), vehicles_.end(), locations.begin(), selector);
            std::transform(vehicles_.begin(), vehicles_.end(), indexes.begin(), selector2);

            g_mutex_veh.unlock();

            int base = 0;
            int k = 0;
            bool matched = false;
            while (!matched && base < n)
            {
                k = base + increment;
                // std::cout << "n: " << n << " k: " << k << std::endl;
                if (k > n)
                    k = n;
                // return value is the ordered indexes of the k locations
                std::vector<int> candidates = gtree_.KNN(request.oid, k, locations);
                int i = base;
                while (!matched && i < k)
                {
                    float distance = 0;
                    int vid = indexes[candidates[i]];
                    Vehicle &veh = vehicles_.at(vid);
                    Route new_route;
                    Schedule new_sched;
                    int s1 = veh.route.size();
                    // originally its lv_stop+1, but that's not correct, due to the distance
                    // from lv_stop to the first in schedule is quite large
                    std::copy(veh.sched.begin() + veh.lv_stop, veh.sched.end(), std::back_inserter(new_sched));
                    if (InsertSchedule(request, veh, new_sched, distance, new_route))
                    {
                        matched = true;
                        // concatenate schedule
                        int insert_pos = 0;
                        for (auto iter = veh.sched.begin(); iter != veh.sched.begin() + veh.lv_stop; ++iter)
                        {
                            new_sched.insert(new_sched.begin() + insert_pos, *iter);
                            insert_pos++;
                        }
                        // compute route and distance
                        Route temp_route;
                        int temp_distance = 0;
                        int start_flag = 0;
                        // concatenate route
                        for (auto iter = new_sched.begin() + 1; iter != new_sched.end(); ++iter)
                        {
                            temp_distance += gtree_.find_path((iter - 1)->node_id, iter->node_id, temp_route);
                            std::copy(temp_route.begin() + start_flag, temp_route.end(), std::back_inserter(new_route));
                            start_flag = 1;
                        }
                        // temporarily just make copy to local map
                        vehicles_[veh.id].sched = new_sched;
                        vehicles_[veh.id].route = new_route;

                        sim_->RequestMatched(request, veh.id, new_sched, new_route);
                    }
                    i++;
                }
                base += increment;
            }
            if (!matched)
                replay_queue_.enqueue(request);
        }
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // t_receive.join();
    std::cout << "done" << std::endl;
}

bool NearestNeighbor::InsertSchedule(const Trip &request, const Vehicle &vehicle, Schedule &schedule, float &distance, Route &new_route)
{
    // +1 get the route from schedule[lv_stop+1]
    // +2 get a copy of schedule[lv_stop+1:end] as test_sch
    // step 1,2 should be done in Run(), schedule here is test_sch
    // +3 insert request into test_sch[begin:end]
    // +4 check the time window
    // +5 if valid, compute the shortest path from begin to end, set as new_route

    // insert with O(n^2)
    // auto o_iter = schedule.begin();
    int o_iter = 1;
    int size = schedule.size();
    int min_distance = std::numeric_limits<int>::max();
    Schedule min_schedule;
    bool found = false;
    // load is negative for not-full vehicle
    int load = vehicle.load;
    // while (o_iter != schedule.end())
    while (o_iter != size)
    {
        // insert pickup stop of the request
        schedule.insert(schedule.begin() + o_iter, Stop{request.id, request.oid, StopType::CUSTOMER_ORIGIN, -1, request.early});
        auto d_iter = o_iter + 1;
        // while (d_iter != schedule.end())
        while (d_iter != size + 1)
        {
            // insert dropoff stop of the request
            schedule.insert(schedule.begin() + d_iter, Stop{request.id, request.did, StopType::CUSTOMER_DEST, -1, request.late});
            // correct the time to first node, using '-' because nnd is negative
            SimTime time = updates_[vehicle.id] - (int)(vehicle.nnd / speed_);
            // check the time window & capacity validity
            bool not_begin = false;
            bool valid = true;
            int total_distance = 0;
            for (auto iter = schedule.begin(); iter != schedule.end(); ++iter)
            {
                if (not_begin)
                {
                    int dis = gtree_.search((iter - 1)->node_id, iter->node_id);
                    total_distance += dis;
                    time += (int)(dis / speed_);
                }
                not_begin = true;
                if (iter->type == StopType::CUSTOMER_ORIGIN || iter->type == StopType::VEHICLE_ORIGIN)
                {
                    // early time window
                    if (time < iter->time_limit)
                        time = iter->time_limit;
                    // if is customer origin, capacity--
                    if (iter->type == StopType::CUSTOMER_ORIGIN)
                        load++;
                    // check capacity validity
                    if (load > 0)
                    {
                        valid = false;
                        break;
                    }
                }
                else
                {
                    // late time window
                    if (time > iter->time_limit)
                    {
                        valid = false;
                        break;
                    }
                    if (iter->type == StopType::CUSTOMER_DEST)
                        load--;
                }
            }
            if (schedule[size + 1].type != StopType::VEHICLE_DEST)
                valid = false;
            if (valid)
            {
                // take into consideration
                if (total_distance < min_distance)
                {
                    min_schedule = schedule;
                    min_distance = total_distance;
                    found = true;
                }
            }
            schedule.erase(schedule.begin() + d_iter);
            d_iter++;
        }
        schedule.erase(schedule.begin() + o_iter);
        o_iter++;
    }
    if (found)
    {
        schedule = min_schedule;
        distance = min_distance;
        // new route. but there may be some problems with the boundray of old route and new route
    }
    return found;
}

/*
Assignments NearestNeighbor(RequestBatch R)
{
    Assignments A;
    const int increment = 10;
    for (int j = 0; j < R.size(); ++j)
    {
        int base = 0;
        int k = 0;
        bool matched = false;
        Request r = R.at(j);
        while (!matched && base <= n)
        {
            k = base + increment;
            std::vector<int> candidates = gtree.KNN(r.origin, k, vehicles);
            int i = base;
            while (!matched && i < k)
            {
                Schedule T;
                Schedule S = candidates[i].schedule;
                double c = schedule_insert_add(S, r, T);
                if (c != -1)
                {
                    A[j] = i;
                    matched = true;
                }
                else
                    ++i;
            }
            if (!matched)
                base += increment;
        }
    }
    return A;
}
*/

int main()
{
    Options op;
    op.RoadNetworkPath = "../data/roadnetworks/cd1.rnet";
    op.GTreePath = "../data/roadnetworks/cd1.gtree";
    op.EdgeFilePath = "../data/roadnetworks/cd1.edges";
    // op.ProblemInstancePath = "../data/benchmarks/cd1/cd1-SR-n10m5-0";
    op.ProblemInstancePath = "../data/dataset_1000+1000_0";
    op.Scale = 5;
    op.VehicleSpeed = 10;
    op.GPSTiming = 5;

    Simulator *sim = new Simulator();
    // move here because solution need options in simulator
    sim->SetOptions(op);
    Solution *solution = new NearestNeighbor(sim);
    sim->SetSolution(solution);
    sim->Initialize();

    std::thread t([&sim, &solution]() {
        sim->Run();
        solution->Terminate();
    });
    solution->Run();
    t.join();
    // solution->Terminate();

    std::cout << "Total match:" << sim->TotalMatch() << std::endl;
    std::cout << "Total time:" << sim->TotalTime() << std::endl;
    std::cout << "Average: " << sim->TotalTime() / sim->TotalMatch() << std::endl;
    std::cout << "Refused rate: " << float(sim->TotalRefuse()) / (sim->TotalMatch() + sim->TotalRefuse()) * 100 << std::endl;
}
