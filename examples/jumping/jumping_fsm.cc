#include "examples/jumping/jumping_fsm.h"



using std::string;
using Eigen::VectorXd;
using drake::systems::Context;
using drake::systems::BasicVector;
using dairlib::systems::OutputVector;
using drake::systems::DiscreteValues;
using drake::systems::DiscreteUpdateEvent;
using drake::systems::EventStatus;

namespace dairlib {
namespace examples {
// namespace jumping {
// namespace osc {

JumpingFiniteStateMachine::JumpingFiniteStateMachine(const RigidBodyTree<double> & tree,
													 double wait_time)
													:wait_time_(wait_time)
														{
	initial_timestamp_ = 0.0;
	state_port_ = this->DeclareVectorInputPort(OutputVector<double>(
													tree.get_num_positions(),
                                   					tree.get_num_velocities(),
                                       				tree.get_num_actuators())).get_index();
	this->DeclareVectorOutputPort(BasicVector<double>(1),
                                	&JumpingFiniteStateMachine::CalcFiniteState);

	DeclarePerStepDiscreteUpdateEvent(&JumpingFiniteStateMachine::DiscreteVariableUpdate);
	// indices for discrete variables in drake leafsystem
	time_idx_ = this->DeclareDiscreteState(1);
	fsm_idx_ = this->DeclareDiscreteState(1);

}

EventStatus JumpingFiniteStateMachine::DiscreteVariableUpdate(const Context<double>& context,
    DiscreteValues<double>* discrete_state) const{

	// placeholder
	bool trigger = false;

	auto fsm_state = discrete_state->get_mutable_vector(fsm_idx_).get_mutable_value();
	auto prev_time = discrete_state->get_mutable_vector(time_idx_).get_mutable_value();

	const OutputVector<double>* robot_output = (OutputVector<double>*)this->EvalVectorInput(context, state_port_);
	double timestamp = robot_output->get_timestamp();
	double current_time = static_cast<double>(timestamp);

	switch((FSM_STATE)fsm_state(0)){
		case(NEUTRAL):
			if(current_time > prev_time(0) + wait_time_){
				fsm_state << CROUCH;
				std::cout << "Setting fsm to CROUCH" << std::endl;
				std::cout << "fsm: " << (FSM_STATE)fsm_state(0) << std::endl;
				prev_time(0) = current_time;
			}
			break;
		case(CROUCH):
			if(current_time > prev_time(0) + 1.0){
				fsm_state << FLIGHT;
				std::cout << "Setting fsm to FLIGHT" << std::endl;
				std::cout << "fsm: " << (FSM_STATE)fsm_state(0) << std::endl;
				prev_time(0) = current_time;
			}
			break;
		case(FLIGHT):
			if(current_time > prev_time(0) + 0.25){
				fsm_state << LAND;
				std::cout << "Setting fsm to LAND" << std::endl;
				std::cout << "fsm: " << (FSM_STATE)fsm_state(0) << std::endl;
				prev_time(0) = current_time;
			}
			break;
		case(LAND):
			// if(current_time > prev_time(0) + 1.25){
			// 	fsm_state << NEUTRAL;
			// 	prev_time(0) = current_time;
			// }
			break;
		default:
			std::cerr << "Invalid state: " << (FSM_STATE)fsm_state(0) << ", defaulting to NEUTRAL: " << NEUTRAL << std::endl;
			fsm_state << NEUTRAL;
	}

	return EventStatus::Succeeded();
}

void JumpingFiniteStateMachine::CalcFiniteState(const Context<double>& context,
												BasicVector<double>* fsm_state) const{

	// const OutputVector<double>* robot_output = (OutputVector<double>*)this->EvalVectorInput(context, state_port_);

	// bool trigger = false;
	// // std::vector<double> ground_reaction_forces = context.getReactionForces();
	// double timestamp = robot_output->get_timestamp();
	// double current_sim_time = static_cast<double>(timestamp);

	// switch(curr_state_){
	// 	case(NEUTRAL):
	// 		if(current_sim_time > initial_timestamp_ + wait_time_){
	// 			curr_state_ = CROUCH;
	// 			break;
	// 		}
	// 	case(CROUCH):
	// 		if(trigger){
	// 			curr_state_ = FLIGHT;
	// 			break;
	// 		}
	// 	case(FLIGHT):
	// 		if(trigger){
	// 			curr_state_ = LAND;
	// 			break;
	// 		}
	// 	case(LAND):
	// 		if(trigger){
	// 			curr_state_ = NEUTRAL;
	// 			break;
	// 		}
	// 	default:
	// 		std::cerr << "Invalid state, defaulting to NEUTRAL." << std::endl;
	// 		curr_state_ = NEUTRAL;
	// }
	// std::cout << "Current state: " << curr_state_ << std::endl;
	// VectorXd current_state(1);
	// current_state << init_state_;
	// fsm_state->get_mutable_value() = current_state;
	fsm_state->get_mutable_value() = context.get_discrete_state().get_vector(fsm_idx_).get_value();
}

// }  // namespace osc
// }  // namespace jumping
}  // namespace examples
}  // namespace dairlib

