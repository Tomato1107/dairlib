#include "examples/Cassie/osc_jump/jumping_event_based_fsm.h"
#include <drake/lcmt_contact_results_for_viz.hpp>
#include "dairlib/lcmt_cassie_mujoco_contact.hpp"

using dairlib::systems::OutputVector;
using drake::multibody::MultibodyPlant;
using drake::systems::BasicVector;
using drake::systems::Context;
using drake::systems::DiscreteUpdateEvent;
using drake::systems::DiscreteValues;
using drake::systems::EventStatus;
using Eigen::VectorXd;
using std::string;
using std::vector;

namespace dairlib {
namespace examples {

JumpingEventFsm::JumpingEventFsm(const MultibodyPlant<double>& plant,
                                 const vector<double>& transition_times,
                                 bool contact_based, double delay_time,
                                 FSM_STATE init_state, SIMULATOR simulator_type)
    : plant_(plant),
      transition_times_(transition_times),
      contact_based_(contact_based),
      transition_delay_(delay_time),
      init_state_(init_state),
      simulator_type_(simulator_type) {
  state_port_ =
      this->DeclareVectorInputPort(OutputVector<double>(plant.num_positions(),
                                                        plant.num_velocities(),
                                                        plant.num_actuators()))
          .get_index();
  if (simulator_type_ == DRAKE) {
    contact_port_ = this->DeclareAbstractInputPort(
                            "lcmt_contact_info",
                            drake::Value<drake::lcmt_contact_results_for_viz>{})
                        .get_index();
  } else if (simulator_type_ == MUJOCO) {
    contact_port_ = this->DeclareAbstractInputPort(
                            "lcmt_contact_info",
                            drake::Value<dairlib::lcmt_cassie_mujoco_contact>{})
                        .get_index();
  } else if (simulator_type_ == GAZEBO) {
  }
  this->DeclareVectorOutputPort(BasicVector<double>(1),
                                &JumpingEventFsm::CalcFiniteState);
  DeclarePerStepDiscreteUpdateEvent(&JumpingEventFsm::DiscreteVariableUpdate);

  BasicVector<double> init_prev_time = BasicVector<double>(VectorXd::Zero(1));
  BasicVector<double> init_state_trigger_time =
      BasicVector<double>(VectorXd::Zero(1));
  BasicVector<double> init_fsm_state = BasicVector<double>(VectorXd::Zero(1));
  init_state_trigger_time.get_mutable_value()(0) = -1.0;
  init_fsm_state.get_mutable_value()(0) = init_state_;

  prev_time_idx_ = this->DeclareDiscreteState(init_prev_time);
  guard_trigger_time_idx_ = this->DeclareDiscreteState(init_state_trigger_time);
  fsm_idx_ = this->DeclareDiscreteState(init_fsm_state);
  transition_flag_idx_ = this->DeclareDiscreteState(1);
}

EventStatus JumpingEventFsm::DiscreteVariableUpdate(
    const Context<double>& context,
    DiscreteValues<double>* discrete_state) const {
  // Get inputs to the leaf system
  const OutputVector<double>* robot_output =
      (OutputVector<double>*)this->EvalVectorInput(context, state_port_);
  const drake::AbstractValue* input =
      this->EvalAbstractInput(context, contact_port_);
  // Get the discrete states
  auto fsm_state =
      discrete_state->get_mutable_vector(fsm_idx_).get_mutable_value();
  auto prev_time =
      discrete_state->get_mutable_vector(prev_time_idx_).get_mutable_value();
  auto state_trigger_time =
      discrete_state->get_mutable_vector(guard_trigger_time_idx_)
          .get_mutable_value();
  auto transition_flag =
      discrete_state->get_mutable_vector(transition_flag_idx_)
          .get_mutable_value();

  int num_contacts = 0;
  if (simulator_type_ == DRAKE) {
    const auto& contact_info_msg =
        input->get_value<drake::lcmt_contact_results_for_viz>();
    num_contacts = contact_info_msg.num_point_pair_contacts;
  } else if (simulator_type_ == MUJOCO) {
    // MuJoCo has "persistent" contact so we have to check contact forces
    // instead of just a boolean value of on/off
    const auto& contact_info_msg =
        input->get_value<dairlib::lcmt_cassie_mujoco_contact>();
    num_contacts = std::count_if(contact_info_msg.contact_forces.begin(),
                                 contact_info_msg.contact_forces.end(),
                                 [&](auto const& force) {
                                   double threshold = 1e-6;
                                   return std::abs(force) >= threshold;
                                 });
  }

  double timestamp = robot_output->get_timestamp();
  auto current_time = static_cast<double>(timestamp);

  if (current_time < prev_time(0)) {  // Simulator has restarted, reset FSM
    std::cout << "Simulator has restarted!" << std::endl;
    fsm_state << init_state_;
    prev_time(0) = current_time;
    transition_flag(0) = false;
  }

  switch ((FSM_STATE)fsm_state(0)) {
    case (BALANCE):
      if (current_time > transition_times_[BALANCE]) {
        fsm_state << CROUCH;
        std::cout << "Current time: " << current_time << std::endl;
        std::cout << "Setting fsm to CROUCH" << std::endl;
        std::cout << "fsm: " << (FSM_STATE)fsm_state(0) << std::endl;
        transition_flag(0) = false;
        prev_time(0) = current_time;
      }
      break;
    case (CROUCH):  // This assumes perfect knowledge about contacts
      if (DetectGuardCondition(contact_based_
                                   ? num_contacts == 0
                                   : current_time > transition_times_[CROUCH],
                               current_time, discrete_state)) {
        state_trigger_time(0) = current_time;
        transition_flag(0) = true;
      }
      if (current_time - state_trigger_time(0) >= transition_delay_ &&
          (bool)transition_flag(0)) {
        fsm_state << FLIGHT;
        std::cout << "Current time: " << current_time << std::endl;
        std::cout << "First detection time: " << state_trigger_time(0) << "\n";
        std::cout << "Setting fsm to FLIGHT" << std::endl;
        std::cout << "fsm: " << (FSM_STATE)fsm_state(0) << std::endl;
        transition_flag(0) = false;
        prev_time(0) = current_time;
      }
      break;
    case (FLIGHT):
      if (DetectGuardCondition(contact_based_
                                   ? num_contacts != 0
                                   : current_time > transition_times_[FLIGHT],
                               current_time, discrete_state)) {
        state_trigger_time(0) = current_time;
        transition_flag(0) = true;
      }
      if (current_time - state_trigger_time(0) >= transition_delay_ &&
          (bool)transition_flag(0)) {
        fsm_state << LAND;
        std::cout << "Current time: " << current_time << "\n";
        std::cout << "First detection time: " << state_trigger_time(0) << "\n";
        std::cout << "Setting fsm to LAND"
                  << "\n";
        std::cout << "fsm: " << (FSM_STATE)fsm_state(0) << "\n";
        transition_flag(0) = false;
        prev_time(0) = current_time;
      }
      break;
    case (LAND):
      break;
  }

  return EventStatus::Succeeded();
}

void JumpingEventFsm::CalcFiniteState(const Context<double>& context,
                                      BasicVector<double>* fsm_state) const {
  fsm_state->get_mutable_value() =
      context.get_discrete_state().get_vector(fsm_idx_).get_value();
}

bool JumpingEventFsm::DetectGuardCondition(
    bool guard_condition, double current_time,
    DiscreteValues<double>* discrete_state) const {
  auto prev_time =
      discrete_state->get_mutable_vector(prev_time_idx_).get_mutable_value();
  auto transition_flag =
      discrete_state->get_mutable_vector(transition_flag_idx_)
          .get_mutable_value();
  // Second condition is to prevent overwriting state_trigger_time
  // Third condition is to prevent floating LCM message from previous
  // simulation from causing unwanted triggers
  return guard_condition && !(bool)transition_flag(0) &&
         (current_time - prev_time(0)) > buffer_time_;
}

}  // namespace examples
}  // namespace dairlib