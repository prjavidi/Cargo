// Copyright (c) 2018 the Cargo authors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
#include <unordered_map>
#include <vector>

#include "libcargo.h"

#include "glpk/glpk.h"

using namespace cargo;

typedef int SharedTripId;
typedef std::vector<Customer> SharedTrip;

class TripVehicleGrouping : public RSAlgorithm {
 public:
  TripVehicleGrouping(const std::string &);

  int unassign_penalty = -1;

  /* My overrides */
  virtual void handle_vehicle(const Vehicle &);
  virtual void match();
  virtual void listen(bool skip_assigned = true, bool skip_delayed = true);

 private:
  Grid grid_;

  /* Vector of GTrees for parallel sp */
  std::vector<GTree::G_Tree> gtre_;


  /* Workspace variables */
  std::unordered_map<CustId, bool> is_matched;
  tick_t timeout_rv_0;
  tick_t timeout_rtv_0;


  SharedTripId stid_;
  dict<CustId, std::vector<SharedTripId>>   cted_;  // cust-trip edges
  dict<SharedTripId, SharedTrip>            trip_;  // trip lookup

  bool travel(const Vehicle &,                // Can this vehl...
              const std::vector<Customer> &,  // ...serve these custs?
              DistInt &,                      // cost of serving
              std::vector<Stop> &,            // resultant schedule
              std::vector<Wayp> &,            // resultant route
              GTree::G_Tree &);               // gtree to use for sp

  SharedTripId add_trip(const SharedTrip &);

  void reset_workspace();
};

