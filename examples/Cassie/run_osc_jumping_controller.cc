#include <drake/lcmt_contact_results_for_viz.hpp>
#include <drake/multibody/parsing/parser.h>
#include <drake/systems/lcm/lcm_interface_system.h>
#include <gflags/gflags.h>
#include "attic/multibody/rigidbody_utils.h"
#include "dairlib/lcmt_robot_input.hpp"
#include "dairlib/lcmt_robot_output.hpp"
#include "examples/Cassie/cassie_utils.h"
#include "examples/Cassie/osc_jump/com_traj_generator.h"
#include "examples/Cassie/osc_jump/flight_foot_traj_generator.h"
#include "examples/Cassie/osc_jump/jumping_event_based_fsm.h"
#include "lcm/lcm_trajectory.h"
#include "systems/controllers/osc/operational_space_control_mbp.h"
#include "systems/controllers/osc/osc_tracking_data.h"
#include "systems/framework/lcm_driven_loop.h"
#include "systems/robot_lcm_systems.h"
#include "drake/multibody/joints/floating_base_types.h"
#include "drake/multibody/rigid_body_tree.h"
#include "drake/multibody/rigid_body_tree_construction.h"
#include "drake/systems/framework/diagram_builder.h"
#include "drake/systems/lcm/lcm_publisher_system.h"

namespace dairlib {

using std::cout;
using std::endl;
using std::map;
using std::string;
using std::vector;

using Eigen::Matrix3d;
using Eigen::MatrixXd;
using Eigen::Vector3d;
using Eigen::VectorXd;

using drake::geometry::SceneGraph;
using drake::multibody::MultibodyPlant;
using drake::multibody::Parser;
using drake::systems::DiagramBuilder;
using drake::systems::TriggerType;
using drake::systems::lcm::LcmPublisherSystem;
using drake::systems::lcm::LcmSubscriberSystem;
using drake::systems::lcm::TriggerTypeSet;
using drake::trajectories::PiecewisePolynomial;
using examples::JumpingEventFsm;
using examples::Cassie::osc_jump::COMTrajGenerator;
using examples::Cassie::osc_jump::FlightFootTrajGenerator;
using multibody::GetBodyIndexFromName;
using systems::controllers::ComTrackingDataMBP;
using systems::controllers::JointSpaceTrackingDataMBP;
using systems::controllers::RotTaskSpaceTrackingDataMBP;
using systems::controllers::TransTaskSpaceTrackingDataMBP;

DEFINE_double(publish_rate, 1000.0, "Target publish rate for OSC");

DEFINE_string(channel_x, "CASSIE_STATE",
              "The name of the channel which receives state");
DEFINE_string(channel_u, "CASSIE_INPUT",
              "The name of the channel which publishes command");
DEFINE_double(balance_height, 1.125,
              "Balancing height for Cassie before attempting the jump");

DEFINE_bool(print_osc, false, "whether to print the osc debug message or not");
DEFINE_bool(is_two_phase, false,
            "true: only right/left single support"
            "false: both double and single support");
DEFINE_string(traj_name, "", "File to load saved trajectories from");

// Currently the controller runs at the rate between 500 Hz and 200 Hz, so the
// publish rate of the robot state needs to be less than 500 Hz. Otherwise, the
// performance seems to degrade due to this. (Recommended publish rate: 200 Hz)
// Maybe we need to update the lcm driven loop to clear the queue of lcm message
// if it's more than one message?

int DoMain(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Drake system initialization stuff
  drake::systems::DiagramBuilder<double> builder;
  SceneGraph<double>& scene_graph = *builder.AddSystem<SceneGraph>();
  scene_graph.set_name("scene_graph");
  MultibodyPlant<double> plant_with_springs(0.0);
  MultibodyPlant<double> plant_without_springs(0.0);
  Parser parser_with_springs(&plant_with_springs, &scene_graph);
  Parser parser_without_springs(&plant_without_springs, &scene_graph);

  /**** Initialize the plants ****/

  parser_with_springs.AddModelFromFile(
      FindResourceOrThrow("examples/Cassie/urdf/cassie_v2.urdf"));
  parser_without_springs.AddModelFromFile(
      FindResourceOrThrow("examples/Cassie/urdf/cassie_fixed_springs.urdf"));
  plant_with_springs.mutable_gravity_field().set_gravity_vector(
      -9.81 * Eigen::Vector3d::UnitZ());
  plant_without_springs.mutable_gravity_field().set_gravity_vector(
      -9.81 * Eigen::Vector3d::UnitZ());
  plant_with_springs.Finalize();
  plant_without_springs.Finalize();

  int n_q = plant_without_springs.num_positions();
  int n_v = plant_without_springs.num_velocities();
  int n_x = n_q + n_v;

  std::cout << "nq: " << n_q << " n_v: " << n_v << " n_x: " << n_x << std::endl;
  // Create maps for joints
  map<string, int> pos_map =
      multibody::makeNameToPositionsMap(plant_without_springs);
  map<string, int> vel_map =
      multibody::makeNameToVelocitiesMap(plant_without_springs);
  map<string, int> act_map =
      multibody::makeNameToActuatorsMap(plant_without_springs);

  /**** Get trajectory from optimization ****/
  const LcmTrajectory& loaded_traj = LcmTrajectory(
      "/home/yangwill/Documents/research/projects/cassie/jumping/saved_trajs/" +
      FLAGS_traj_name);

  const LcmTrajectory::Trajectory& com_traj =
      loaded_traj.getTrajectory("center_of_mass_trajectory");
  const LcmTrajectory::Trajectory& lcm_l_foot_traj =
      loaded_traj.getTrajectory("left_foot_trajectory");
  const LcmTrajectory::Trajectory& lcm_r_foot_traj =
      loaded_traj.getTrajectory("right_foot_trajectory");
  const LcmTrajectory::Trajectory& com_vel_traj =
      loaded_traj.getTrajectory("center_of_mass_vel_trajectory");
  const LcmTrajectory::Trajectory& lcm_l_foot_vel_traj =
      loaded_traj.getTrajectory("left_foot_vel_trajectory");
  const LcmTrajectory::Trajectory& lcm_r_foot_vel_traj =
      loaded_traj.getTrajectory("right_foot_vel_trajectory");
  //  const LcmTrajectory::Trajectory& lcm_torso_traj =
  //      loaded_traj.getTrajectory("torso_trajectory");
  //  cout << "com_vel:" << com_vel_traj.datapoints.size() << endl;
  //  cout << "l_foot_vel: " << lcm_l_foot_vel_traj.datapoints.size() << endl;
  //  cout << "r_foot_vel: " << lcm_r_foot_vel_traj.datapoints.size() << endl;
  const PiecewisePolynomial<double>& center_of_mass_traj =
      PiecewisePolynomial<double>::Cubic(
          com_traj.time_vector, com_traj.datapoints, com_vel_traj.datapoints);
  const PiecewisePolynomial<double>& l_foot_trajectory =
      PiecewisePolynomial<double>::Cubic(lcm_l_foot_traj.time_vector,
                                         lcm_l_foot_traj.datapoints,
                                         lcm_l_foot_vel_traj.datapoints);
  const PiecewisePolynomial<double>& r_foot_trajectory =
      PiecewisePolynomial<double>::Cubic(lcm_r_foot_traj.time_vector,
                                         lcm_r_foot_traj.datapoints,
                                         lcm_r_foot_vel_traj.datapoints);
  //  const PiecewisePolynomial<double>& torso_trajectory =
  //      PiecewisePolynomial<double>::Pchip(lcm_torso_traj.time_vector,
  //          lcm_torso_traj.datapoints);

  double flight_time = 0.0;
  double land_time = 0.0;

  /**** Initialize all the leaf systems ****/

  // Cassie parameters
  Vector3d front_contact_disp(-0.0457, 0.112, 0);
  Vector3d rear_contact_disp(0.088, 0, 0);
//  Vector3d mid_contact_disp = (front_contact_disp + rear_contact_disp) / 2;

  // Get body indices for cassie with springs
  auto pelvis_idx = plant_with_springs.GetBodyByName("pelvis").index();
//  auto l_toe_idx = plant_with_springs.GetBodyByName("toe_left").index();
//  auto r_toe_idx = plant_with_springs.GetBodyByName("toe_right").index();

  //  auto lcm = builder.AddSystem<drake::systems::lcm::LcmInterfaceSystem>();
  drake::lcm::DrakeLcm lcm;
  auto contact_results_sub = builder.AddSystem(
      LcmSubscriberSystem::Make<drake::lcmt_contact_results_for_viz>(
          "CONTACT_RESULTS", &lcm));
  //  auto state_sub = builder.AddSystem(
  //      LcmSubscriberSystem::Make<lcmt_robot_output>(FLAGS_channel_x, lcm));
  auto state_receiver =
      builder.AddSystem<systems::RobotOutputReceiver>(plant_without_springs);
  // Create Operational space control
  auto com_traj_generator = builder.AddSystem<COMTrajGenerator>(
      plant_with_springs, pelvis_idx, front_contact_disp, rear_contact_disp,
      center_of_mass_traj, FLAGS_balance_height);
  auto l_foot_traj_generator = builder.AddSystem<FlightFootTrajGenerator>(
      plant_with_springs, "pelvis", true, l_foot_trajectory);
  auto r_foot_traj_generator = builder.AddSystem<FlightFootTrajGenerator>(
      plant_with_springs, "pelvis", false, r_foot_trajectory);
  //  auto pelvis_orientation_traj_generator =
  //      builder.AddSystem<TorsoTraj>(plant_with_springs,
  //      pelvis_orientation_traj);
  auto fsm = builder.AddSystem<dairlib::examples::JumpingEventFsm>(
      plant_with_springs, flight_time, land_time);
  auto command_pub =
      builder.AddSystem(LcmPublisherSystem::Make<dairlib::lcmt_robot_input>(
          FLAGS_channel_u, &lcm, 1.0 / FLAGS_publish_rate));
  auto command_sender =
      builder.AddSystem<systems::RobotCommandSender>(plant_with_springs);
  auto osc =
      builder.AddSystem<systems::controllers ::OperationalSpaceControlMBP>(
          plant_with_springs, plant_without_springs, true,
          FLAGS_print_osc); /*print_tracking_info*/

  /**** OSC setup ****/

  // Cost
  MatrixXd Q_accel = 0.001 * MatrixXd::Identity(n_v, n_v);
  osc->SetAccelerationCostForAllJoints(Q_accel);
  // Soft constraint on contacts
  double w_contact_relax = 20000;
  osc->SetWeightOfSoftContactConstraint(w_contact_relax);
  double mu = 0.4;
  // Contact information for OSC
  osc->SetContactFriction(mu);
  vector<int> stance_modes = {0, 2};
  for (int mode : stance_modes) {
    osc->AddStateAndContactPoint(mode, "toe_left", front_contact_disp);
    osc->AddStateAndContactPoint(mode, "toe_left", rear_contact_disp);
    osc->AddStateAndContactPoint(mode, "toe_right", front_contact_disp);
    osc->AddStateAndContactPoint(mode, "toe_right", rear_contact_disp);
  }

  /**** Tracking Data for OSC *****/
  // Center of mass tracking
  MatrixXd W_com = MatrixXd::Identity(3, 3);
  W_com(0, 0) = 2;
  W_com(1, 1) = 2;
  W_com(2, 2) = 2000;
  MatrixXd K_p_com = 50 * MatrixXd::Identity(3, 3);
  MatrixXd K_d_com = 10 * MatrixXd::Identity(3, 3);
  ComTrackingDataMBP com_tracking_data("com_traj", 3, K_p_com, K_d_com, W_com,
                                       &plant_with_springs,
                                       &plant_without_springs);
  osc->AddTrackingData(&com_tracking_data);

  // Feet tracking
  MatrixXd W_swing_foot = 1 * MatrixXd::Identity(3, 3);
  W_swing_foot(0, 0) = 1000;
  W_swing_foot(1, 1) = 0;
  W_swing_foot(2, 2) = 1000;
  MatrixXd K_p_sw_ft = 100 * MatrixXd::Identity(3, 3);
  MatrixXd K_d_sw_ft = 20 * MatrixXd::Identity(3, 3);
  K_p_sw_ft(1, 1) = 0;
  K_d_sw_ft(1, 1) = 0;

  TransTaskSpaceTrackingDataMBP flight_phase_left_foot_traj(
      "l_foot_traj", 3, K_p_sw_ft, K_d_sw_ft, W_swing_foot, &plant_with_springs,
      &plant_without_springs);
  TransTaskSpaceTrackingDataMBP flight_phase_right_foot_traj(
      "r_foot_traj", 3, K_p_sw_ft, K_d_sw_ft, W_swing_foot, &plant_with_springs,
      &plant_without_springs);
  flight_phase_left_foot_traj.AddStateAndPointToTrack(examples::FLIGHT,
                                                      "toe_left");
  flight_phase_right_foot_traj.AddStateAndPointToTrack(examples::FLIGHT,
                                                       "toe_right");
  osc->AddTrackingData(&flight_phase_left_foot_traj);
  osc->AddTrackingData(&flight_phase_right_foot_traj);

  // Build OSC problem
  osc->Build();

  /*****Connect ports*****/

  // State receiver connections (Connected through LCM driven loop)

  // OSC connections
  builder.Connect(fsm->get_output_port(0), osc->get_fsm_input_port());
  builder.Connect(state_receiver->get_output_port(0),
                  osc->get_robot_output_input_port());
  builder.Connect(com_traj_generator->get_output_port(0),
                  osc->get_tracking_data_input_port("com_traj"));
  builder.Connect(l_foot_traj_generator->get_output_port(0),
                  osc->get_tracking_data_input_port("left_foot"));
  builder.Connect(r_foot_traj_generator->get_output_port(0),
                  osc->get_tracking_data_input_port("right_foot"));

  // FSM connections
  builder.Connect(contact_results_sub->get_output_port(),
                  fsm->get_contact_input_port());
  builder.Connect(state_receiver->get_output_port(0),
                  fsm->get_state_input_port());

  // Trajectory generator connections
  builder.Connect(fsm->get_output_port(0),
                  com_traj_generator->get_fsm_input_port());
  builder.Connect(state_receiver->get_output_port(0),
                  l_foot_traj_generator->get_state_input_port());
  builder.Connect(state_receiver->get_output_port(0),
                  r_foot_traj_generator->get_state_input_port());
  builder.Connect(state_receiver->get_output_port(0),
                  com_traj_generator->get_fsm_input_port());
  builder.Connect(fsm->get_output_port(0),
                  l_foot_traj_generator->get_fsm_input_port());
  builder.Connect(fsm->get_output_port(0),
                  r_foot_traj_generator->get_fsm_input_port());

  // Publisher connections
  builder.Connect(osc->get_output_port(0), command_sender->get_input_port(0));
  builder.Connect(command_sender->get_output_port(0),
                  command_pub->get_input_port());

  // Run lcm-driven simulation
  // Create the diagram
  auto owned_diagram = builder.Build();
  owned_diagram->set_name(("osc jumping controller"));

  // Run lcm-driven simulation
  systems::LcmDrivenLoop<dairlib::lcmt_robot_output> loop(
      &lcm, std::move(owned_diagram), state_receiver, FLAGS_channel_x, false);
  loop.Simulate();

  return 0;
}

}  // namespace dairlib

int main(int argc, char* argv[]) { return dairlib::DoMain(argc, argv); }