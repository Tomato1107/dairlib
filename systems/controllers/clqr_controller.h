#ifndef CLQR_CONTROLLER_H
#define CLQR_CONTROLLER_H

#include "drake/systems/framework/leaf_system.h"
#include "drake/systems/framework/basic_vector.h"
#include "drake/multibody/rigid_body_plant/rigid_body_plant.h"
#include "drake/multibody/rigid_body_tree.h"
#include "drake/math/autodiff.h"
#include "drake/math/autodiff_gradient.h"

#include "systems/controllers/linear_controller.h"

#include <iomanip>

using Eigen::VectorXd;
using Eigen::Matrix;
using Eigen::MatrixXd;
using Eigen::Dynamic;
using Eigen::HouseholderQR;
using drake::AutoDiffXd;
using drake::AutoDiffVecXd;
using drake::systems::RigidBodyPlant;
using drake::systems::Context;
using drake::systems::BasicVector;
using drake::systems::LeafSystem;
using drake::systems::InputPortDescriptor;
using drake::systems::OutputPort;

namespace dairlib{
namespace systems{

class ClqrController : public LinearController
{
    public: 

        ClqrController(const RigidBodyTree<double>& tree, RigidBodyPlant<double>* plant, VectorXd x0_, int num_positions, int num_velocities, int num_actuators);
        //const InputPortDescriptor<double>& getInputStatePort();
        //const InputPortDescriptor<double>& getInputDesiredPort();
        //const OutputPort<double>& getOutputActuatorPort();
        int getNumPositions();
        int getNumVelocities();
        int getNumStates();
        int getNumActuators();
        Matrix<double, Dynamic, Dynamic> getQ();
        Matrix<double, Dynamic, Dynamic> getR();
        Matrix<double, Dynamic, Dynamic> getK();
        void setK(Matrix<double, Dynamic, Dynamic> K);
        void setQ(Matrix<double, Dynamic, Dynamic>, Q);
        void setR(Matrix<double, Dynamic, Dynamic>, R);


    private:

        //void calcControl(const Context<double>& context, BasicVector<double>*output) const;
        Matrix<double, Dynamic, Dynamic> computeF();
        Matrix<double, Dynamic, Dynamic> computeK();
        const RigidBodyTree<double>& tree_;
        RigidBodyPlant<double>* plant_;
        VectorXd x0_;
        //int input_state_port_index_;
        //int input_desired_port_index_;
        //int output_actuator_port_index_;
        int num_positions_;
        int num_velocities_;
        int num_states_;
        int num_actuators_;
        Matrix<double, Dynamic, Dynamic> Q_;
        Matrix<double, Dynamic, Dynamic> R_;
        Matrix<double, Dynamic, Dynamic> F_;
        Matrix<double, Dynamic, Dynamic> K_;
};

}// namespace systems
}//namespace dairlib




#endif