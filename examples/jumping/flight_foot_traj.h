#pragma once

#include "drake/multibody/rigid_body_tree.h"
#include "drake/systems/framework/leaf_system.h"
#include "drake/common/trajectories/piecewise_polynomial.h"
#include "systems/framework/output_vector.h"
#include "systems/controllers/control_utils.h"
#include "attic/multibody/rigidbody_utils.h"

using drake::trajectories::PiecewisePolynomial;
using Eigen::VectorXd;

namespace dairlib {
namespace examples{
namespace jumping {
namespace osc {

class FlightFootTraj : public drake::systems::LeafSystem<double> {
public:
  FlightFootTraj(const RigidBodyTree<double>& tree,
				int hip_idx,
				int left_foot_idx,
				int right_foot_idx,
        bool isLeftFoot,
				double height = 0.7,
        double foot_offset = 0.1);

  const drake::systems::InputPort<double>& get_state_input_port() const {
    return this->get_input_port(state_port_);
  }
  const drake::systems::InputPort<double>& get_fsm_input_port() const {
    return this->get_input_port(fsm_port_);
  }

private:
  PiecewisePolynomial<double> generateFlightTraj( const drake::systems::Context<double>& context,
                                                  VectorXd& q, VectorXd& v) const;

  // drake::systems::EventStatus DiscreteVariableUpdate(const drake::systems::Context<double>& context,
  //                   drake::systems::DiscreteValues<double>* discrete_state) const;

  void CalcTraj(const drake::systems::Context<double>& context,
				 drake::trajectories::Trajectory<double>* traj) const;

  const RigidBodyTree<double>& tree_;
  int hip_idx_;
  int left_foot_idx_;
  int right_foot_idx_;
  bool isLeftFoot_;
  double height_;
  double foot_offset_;

  int state_port_;
  int fsm_port_;

  // Eigen::Vector3d front_contact_disp_ = Eigen::Vector3d(-0.0457, 0.112, 0);
  // Eigen::Vector3d rear_contact_disp_ = Eigen::Vector3d(0.088, 0, 0);

  //testing
  std::unique_ptr<double> first_msg_time_;
};

}  // namespace osc
}  // namespace jumping
}  // namespace examples
}  // namespace dairlib

