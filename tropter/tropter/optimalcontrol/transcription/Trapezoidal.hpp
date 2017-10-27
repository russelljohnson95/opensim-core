#ifndef MUSCOLLO_TRANSCRIPTION_TRAPEZOIDAL_HPP
#define MUSCOLLO_TRANSCRIPTION_TRAPEZOIDAL_HPP
// ----------------------------------------------------------------------------
// tropter: Trapezoidal.hpp
// ----------------------------------------------------------------------------
// Copyright (c) 2017 tropter authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain a
// copy of the License at http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------

#include "Trapezoidal.h"

#include <tropter/Exception.hpp>

#include <iomanip>

namespace tropter {
namespace transcription {

template<typename T>
void Trapezoidal<T>::set_ocproblem(
        std::shared_ptr<const OCProblem> ocproblem) {
    m_ocproblem = ocproblem;
    m_num_states = m_ocproblem->get_num_states();
    m_num_controls = m_ocproblem->get_num_controls();
    m_num_continuous_variables = m_num_states+m_num_controls;
    m_num_time_variables = 2;
    int num_variables = m_num_time_variables
            + m_num_mesh_points * m_num_continuous_variables;
    this->set_num_variables(num_variables);
    m_num_defects = m_num_mesh_points - 1;
    m_num_dynamics_constraints = m_num_defects * m_num_states;
    m_num_path_constraints = m_ocproblem->get_num_path_constraints();
    // TODO rename..."total_path_constraints"?
    int num_path_traj_constraints = m_num_mesh_points * m_num_path_constraints;
    int num_constraints = m_num_dynamics_constraints +
            num_path_traj_constraints;
    this->set_num_constraints(num_constraints);

    // Bounds.
    double initial_time_lower;
    double initial_time_upper;
    double final_time_lower;
    double final_time_upper;
    using Eigen::VectorXd;
    VectorXd states_lower(m_num_states);
    VectorXd states_upper(m_num_states);
    VectorXd initial_states_lower(m_num_states);
    VectorXd initial_states_upper(m_num_states);
    VectorXd final_states_lower(m_num_states);
    VectorXd final_states_upper(m_num_states);
    VectorXd controls_lower(m_num_controls);
    VectorXd controls_upper(m_num_controls);
    VectorXd initial_controls_lower(m_num_controls);
    VectorXd initial_controls_upper(m_num_controls);
    VectorXd final_controls_lower(m_num_controls);
    VectorXd final_controls_upper(m_num_controls);
    VectorXd path_constraints_lower(m_num_path_constraints);
    VectorXd path_constraints_upper(m_num_path_constraints);
    m_ocproblem->get_all_bounds(initial_time_lower, initial_time_upper,
            final_time_lower, final_time_upper,
            states_lower, states_upper,
            initial_states_lower, initial_states_upper,
            final_states_lower, final_states_upper,
            controls_lower, controls_upper,
            initial_controls_lower, initial_controls_upper,
            final_controls_lower, final_controls_upper,
            path_constraints_lower, path_constraints_upper);
    // TODO validate sizes.
    // Bounds on variables.
    VectorXd variable_lower(num_variables);
    variable_lower <<
            initial_time_lower, final_time_lower,
            initial_states_lower, initial_controls_lower,
            (VectorXd(m_num_continuous_variables)
                    << states_lower, controls_lower)
                    .finished()
                    .replicate(m_num_mesh_points - 2, 1),
            final_states_lower, final_controls_lower;
    VectorXd variable_upper(num_variables);
    variable_upper <<
            initial_time_upper, final_time_upper,
            initial_states_upper, initial_controls_upper,
            (VectorXd(m_num_continuous_variables)
                    << states_upper, controls_upper)
                    .finished()
                    .replicate(m_num_mesh_points - 2, 1),
            final_states_upper, final_controls_upper;
    this->set_variable_bounds(variable_lower, variable_upper);
    // Bounds for constraints.
    VectorXd constraint_lower(num_constraints);
    VectorXd constraint_upper(num_constraints);
    // Defects must be 0.
    VectorXd dynamics_bounds = VectorXd::Zero(m_num_dynamics_constraints);
    VectorXd path_constraints_traj_lower =
            path_constraints_lower.replicate(m_num_mesh_points, 1);
    VectorXd path_constraints_traj_upper =
            path_constraints_upper.replicate(m_num_mesh_points, 1);
    constraint_lower << dynamics_bounds, path_constraints_traj_lower;
    constraint_upper << dynamics_bounds, path_constraints_traj_upper;
    this->set_constraint_bounds(constraint_lower, constraint_upper);
    // TODO won't work if the bounds don't include zero!
    // TODO set_initial_guess(std::vector<double>(num_variables)); // TODO user
    // input

    // Set the tropter.
    // -------------
    const unsigned num_mesh_intervals = m_num_mesh_points - 1;
    // For integrating the integral cost.
    // The duration of each tropter interval.
    VectorXd mesh = VectorXd::LinSpaced(m_num_mesh_points, 0, 1);
    VectorXd mesh_intervals = mesh.tail(num_mesh_intervals)
            - mesh.head(num_mesh_intervals);
    m_trapezoidal_quadrature_coefficients = VectorXd::Zero(m_num_mesh_points);
    // Betts 2010 equation 4.195, page 169.
    // b = 0.5 * [tau0, tau0 + tau1, tau1 + tau2, ..., tauM-2 + tauM-1, tauM-1]
    m_trapezoidal_quadrature_coefficients.head(num_mesh_intervals) =
            0.5 * mesh_intervals;
    m_trapezoidal_quadrature_coefficients.tail(num_mesh_intervals) +=
            0.5 * mesh_intervals;

    // Allocate working memory.
    m_integrand.resize(m_num_mesh_points);
    m_derivs.resize(m_num_states, m_num_mesh_points);

    m_ocproblem->initialize_on_mesh(mesh);
}

template<typename T>
void Trapezoidal<T>::calc_objective(const VectorX<T>& x, T& obj_value) const
{
    // TODO move this to a "make_variables_view()"
    const T& initial_time = x[0];
    const T& final_time = x[1];
    const T duration = final_time - initial_time;
    const T step_size = duration / (m_num_mesh_points - 1);

    // TODO I don't actually need to make a new view each time; just change the
    // data pointer. TODO probably don't even need to update the data pointer!
    auto states = make_states_trajectory_view(x);
    auto controls = make_controls_trajectory_view(x);

    // Endpoint cost.
    // --------------
    // TODO does this cause the final_states to get copied?
    m_ocproblem->calc_endpoint_cost(final_time, states.rightCols(1), obj_value);


    // Integral cost.
    // --------------
    m_integrand.setZero();
    for (int i_mesh = 0; i_mesh < m_num_mesh_points; ++i_mesh) {
        const T time = step_size * i_mesh + initial_time;
        m_ocproblem->calc_integral_cost(time,
                states.col(i_mesh), controls.col(i_mesh), m_integrand[i_mesh]);
    }
    // TODO use more intelligent quadrature? trapezoidal rule?
    // Rectangle rule:
    //obj_value = integrand[0]
    //        + step_size * integrand.tail(m_num_mesh_points - 1).sum();
    // The left vector is of type T b/c the dot product requires the same type.
    // TODO the following doesn't work because of different numerical types.
    // obj_value = m_trapezoidal_quadrature_coefficients.dot(integrand);
    T integral_cost = 0;
    for (int i_mesh = 0; i_mesh < m_num_mesh_points; ++i_mesh) {
        integral_cost += m_trapezoidal_quadrature_coefficients[i_mesh] *
                m_integrand[i_mesh];
    }
    // The quadrature coefficients are fractions of the duration; multiply
    // by duration to get the correct units.
    integral_cost *= duration;
    obj_value += integral_cost;
}

template<typename T>
void Trapezoidal<T>::calc_constraints(const VectorX<T>& x,
        Eigen::Ref<VectorX<T>> constraints) const
{
    // TODO parallelize.
    const T& initial_time = x[0];
    const T& final_time = x[1];
    const T duration = final_time - initial_time;
    const T step_size = duration / (m_num_mesh_points - 1);

    auto states = make_states_trajectory_view(x);
    auto controls = make_controls_trajectory_view(x);

    // Organize the constrants vector.
    ConstraintsView constr_view = make_constraints_view(constraints);

    // Dynamics and path constraints.
    // ==============================
    // "Continuous function"

    // Obtain state derivatives at each tropter point.
    // --------------------------------------------
    // TODO storing 1 too many derivatives trajectory; don't need the first
    // xdot (at t0). (TODO I don't think this is true anymore).
    // TODO tradeoff between memory and parallelism.
    for (int i_mesh = 0; i_mesh < m_num_mesh_points; ++i_mesh) {
        // TODO should pass the time.
        const T time = step_size * i_mesh + initial_time;
        m_ocproblem->calc_differential_algebraic_equations(
                {i_mesh, time, states.col(i_mesh), controls.col(i_mesh)},
                {m_derivs.col(i_mesh),
                 constr_view.path_constraints.col(i_mesh)});
        //m_ocproblem->calc_differential_algebraic_equations(i_mesh, time,
        //        states.col(i_mesh), controls.col(i_mesh),
        //        m_derivs.col(i_mesh), constr_view.path_constraints.col(i_mesh));
    }

    // Compute constraint defects.
    // ---------------------------
    // Backwards Euler (not used here):
    // defect_i = x_i - (x_{i-1} + h * xdot_i)  for i = 1, ..., N.
    const unsigned N = m_num_mesh_points;
    const auto& x_i = states.rightCols(N - 1);
    const auto& x_im1 = states.leftCols(N - 1);
    const auto& xdot_i = m_derivs.rightCols(N - 1);
    const auto& h = step_size;
    //constr_view.defects = x_i-(x_im1+h*xdot_i);
    // TODO Trapezoidal:
    const auto& xdot_im1 = m_derivs.leftCols(N-1);
    //constr_view.defects = x_i-(x_im1+h*xdot_im1);
    constr_view.defects = x_i - (x_im1 + 0.5 * h * (xdot_i + xdot_im1));
    //for (int i_mesh = 0; i_mesh < N - 1; ++i_mesh) {
    //    const auto& h = m_mesh_intervals[i_mesh];
    //    constr_view.defects.col(i_mesh) = x_i.col(i_mesh)
    //            - (x_im1.col(i_mesh) + 0.5 * h * duration * (xdot_i.col
    // (i_mesh) + xdot_im1.col(i_mesh)));
    //}

}

template<typename T>
Eigen::VectorXd Trapezoidal<T>::
construct_iterate(const OptimalControlIterate& traj, bool interpolate) const
{
    // Check for errors with dimensions.
    // ---------------------------------
    // TODO move some of this to OptimalControlIterate::validate().
    // Check rows.
    TROPTER_THROW_IF(traj.states.rows() != m_num_states,
            "Expected states to have %i row(s), but it has %i.",
            m_num_states, traj.states.rows());
    TROPTER_THROW_IF(traj.controls.rows() != m_num_controls,
            "Expected controls to have %i row(s), but it has %i.",
            m_num_controls, traj.controls.rows());
    // Check columns.
    if (interpolate) {
        TROPTER_THROW_IF(   traj.time.size() != traj.states.cols()
                        || traj.time.size() != traj.controls.cols(),
                "Expected time, states, and controls to have the same number "
                        "of columns (they have %i, %i, %i column(s), "
                        "respectively).", traj.time.size(), traj.states.cols(),
                traj.controls.size());
    } else {
        TROPTER_THROW_IF(traj.time.size() != m_num_mesh_points,
                "Expected time to have %i element(s), but it has %i.",
                m_num_mesh_points, traj.time.size());
        TROPTER_THROW_IF(traj.states.cols() != m_num_mesh_points,
                "Expected states to have %i column(s), but it has %i.",
                m_num_mesh_points, traj.states.cols());
        TROPTER_THROW_IF(traj.controls.cols() != m_num_mesh_points,
                "Expected controls to have %i column(s), but it has %i.",
                m_num_mesh_points, traj.controls.cols());
    }

    // Interpolate the guess, as it might have a different number of mesh
    // points than m_num_mesh_points.
    OptimalControlIterate traj_interp;
    const OptimalControlIterate* traj_to_use;
    if (interpolate) {
        // TODO will actually need to provide the mesh spacing as well, when we
        // no longer have uniform mesh spacing.
        traj_interp = traj.interpolate(m_num_mesh_points);
        traj_to_use = &traj_interp;
    } else {
        traj_to_use = &traj;
    }

    Eigen::VectorXd iterate(this->get_num_variables());
    // Initial and final time.
    iterate[0] = traj_to_use->time[0];
    iterate[1] = traj_to_use->time.tail<1>()[0];
    // Create mutable views. This will probably fail miserably if the
    // dimensions do not match.
    this->make_states_trajectory_view(iterate) = traj_to_use->states;
    this->make_controls_trajectory_view(iterate) = traj_to_use->controls;
    return iterate;
}

template<typename T>
OptimalControlIterate Trapezoidal<T>::
deconstruct_iterate(const Eigen::VectorXd& x) const
{
    // TODO move time variables to the end.
    const double& initial_time = x[0];
    const double& final_time = x[1];
    OptimalControlIterate traj;
    traj.time = Eigen::RowVectorXd::LinSpaced(m_num_mesh_points,
            initial_time, final_time);

    traj.states = this->make_states_trajectory_view(x);
    traj.controls = this->make_controls_trajectory_view(x);

    traj.state_names = m_ocproblem->get_state_names();
    traj.control_names = m_ocproblem->get_control_names();

    return traj;
}

template<typename T>
void Trapezoidal<T>::
print_constraint_values(const OptimalControlIterate& ocp_vars,
        std::ostream& stream) const
{
    // TODO also print_bounds() information.
    // TODO allow passing an option for only showing when bounds are
    // violated, not simply active.
    // TODO allow passing a threshold to see if a value is within range of a
    // bound.

    // We want to be able to restore the stream's original formatting.
    std::ios orig_fmt(nullptr);
    orig_fmt.copyfmt(stream);

    // Gather and organize all constraint values and bounds.
    VectorX<T> vars = construct_iterate(ocp_vars).template cast<T>();
    VectorX<T> constraint_values(this->get_num_constraints());
    calc_constraints(vars, constraint_values);
    ConstraintsView values = make_constraints_view(constraint_values);

    // TODO avoid cast by templatizing make_constraints_view().
    VectorX<T> lower_T =
            this->get_constraint_lower_bounds().template cast<T>();
    //ConstraintsView lower = make_constraints_view(lower_T);
    VectorX<T> upper_T =
            this->get_constraint_upper_bounds().template cast<T>();
    //ConstraintsView upper = make_constraints_view(upper_T);
    auto state_names = m_ocproblem->get_state_names();
    auto control_names = m_ocproblem->get_control_names();

    OptimalControlIterate ocp_vars_lower = deconstruct_iterate(
            this->get_variable_lower_bounds());
    OptimalControlIterate ocp_vars_upper = deconstruct_iterate(
            this->get_variable_upper_bounds());

    // TODO handle the case where there are no states or no controls.

    // Find the longest state or control name.
    auto compare_size = [](const std::string& a, const std::string& b) {
        return a.size() < b.size();
    };
    int max_name_length = 0;
    if (!state_names.empty()) {
        max_name_length = (int)std::max_element(state_names.begin(),
                state_names.end(),
                compare_size)->size();
    }
    if (!control_names.empty()) {
        max_name_length = (int)std::max((size_t)max_name_length,
                std::max_element(control_names.begin(),
                        control_names.end(),
                        compare_size)->size());
    }

    stream << "\nActive or violated bounds" << std::endl;
    stream << "L and U indicate which bound is active; "
            "'*' indicates a bound is violated. " << std::endl;
    stream << "The case of lower==upper==value is ignored." << std::endl;

    // TODO bounds on initial and final time.

    // Bounds on state and control variables.
    // --------------------------------------
    using Eigen::RowVectorXd;
    using Eigen::MatrixXd;
    auto print_bounds = [&stream, max_name_length](
            const std::string& description,
            const std::vector<std::string>& names,
            const RowVectorXd& times, const MatrixXd& values,
            const MatrixXd& lower, const MatrixXd& upper) {
        // TODO
        stream << "\n" << description << ": ";

        bool bounds_active = false;
        bool bounds_violated = false;
        for (Eigen::Index ivar = 0; ivar < values.rows(); ++ivar) {
            for (Eigen::Index itime = 0; itime < times.size(); ++itime) {
                const auto& L = lower(ivar, itime);
                const auto& V = values(ivar, itime);
                const auto& U = upper(ivar, itime);
                if (V <= L || V >= U) {
                    if (V == L && L == U) continue;
                    bounds_active = true;
                    if (V < L || V > U) {
                        bounds_violated = true;
                        break;
                    }
                }
            }
        }

        if (!bounds_active && !bounds_violated) {
            stream << "no bounds active or violated" << std::endl;
            return;
        }

        if (!bounds_violated) {
            stream << "some bounds active but no bounds violated";
        } else {
            stream << "some bounds active or violated";

        }

        stream << "\n" << std::setw(max_name_length) << "  "
                << std::setw(9) << "time " << "  "
                << std::setw(9) << "lower" << "    "
                << std::setw(9) << "value" << "    "
                << std::setw(9) << "upper" << " " << std::endl;

        for (Eigen::Index ivar = 0; ivar < values.rows(); ++ivar) {
            for (Eigen::Index itime = 0; itime < times.size(); ++itime) {
                const auto& L = lower(ivar, itime);
                const auto& V = values(ivar, itime);
                const auto& U = upper(ivar, itime);
                if (V <= L || V >= U) {
                    // In the case where lower==upper==value, there is no
                    // issue; ignore.
                    if (V == L && L == U) continue;
                    const auto& time = times[itime];
                    stream << std::setw(max_name_length) << names[ivar] << "  "
                            << std::setprecision(2) << std::scientific
                            << std::setw(9) << time << "  "
                            << std::setw(9) << L << " <= "
                            << std::setw(9) << V << " <= "
                            << std::setw(9) << U << " ";
                    // Show if the constraint is violated.
                    if (V <= L) stream << " ";
                    else        stream << "L";
                    if (V >= U) stream << " ";
                    else        stream << "U";
                    if (V < L || V > U) stream << "*";
                    stream << std::endl;
                }
            }
        }
    };
    print_bounds("State bounds", state_names, ocp_vars.time,
            ocp_vars.states, ocp_vars_lower.states, ocp_vars_upper.states);
    print_bounds("Control bounds", control_names, ocp_vars.time,
            ocp_vars.controls, ocp_vars_lower.controls, ocp_vars_upper.controls);

    // Constraints.
    // ============

    stream << "\nTotal number of constraints: "
            << constraint_values.size() << "." << std::endl;

    // Differential equation defects.
    // ------------------------------
    stream << "\nDifferential equation defects:" << std::endl;
    stream << std::setw(max_name_length) << " " << "  norm across the mesh"
            << std::endl;
    std::string spacer(7, ' ');
    for (size_t i_state = 0; i_state < state_names.size(); ++i_state) {
        auto& norm = static_cast<const double&>(
                values.defects.row(i_state).norm());

        stream << std::setw(max_name_length) << state_names[i_state]
                << spacer
                << std::setprecision(2) << std::scientific << std::setw(9)
                << norm << std::endl;
    }

    // Path constraints.
    // -----------------
    stream << "\nPath constraints:";
    auto pathcon_names = m_ocproblem->get_path_constraint_names();

    if (pathcon_names.empty()) {
        stream << " none" << std::endl;
        // Return early if there are no path constraints.
        return;
    }
    stream << std::endl;

    const int max_pathcon_name_length =
            (int)std::max_element(pathcon_names.begin(), pathcon_names.end(),
                    compare_size)->size();
    stream << std::setw(max_pathcon_name_length) << " "
            << "  norm across the mesh" << std::endl;
    for (size_t i_pc = 0; i_pc < pathcon_names.size(); ++i_pc) {
        auto& norm = static_cast<const double&>(
                values.path_constraints.row(i_pc).norm());
        stream << std::setw(2) << i_pc << ":"
                << std::setw(max_pathcon_name_length) << pathcon_names[i_pc]
                << spacer
                << std::setprecision(2) << std::scientific << std::setw(9)
                << norm << std::endl;
    }
    stream << "Path constraint values at each mesh point:" << std::endl;
    for (size_t i_pc = 0; i_pc < pathcon_names.size(); ++i_pc) {
        stream << std::setw(9) << i_pc << "  ";
    }
    stream << std::endl;
    for (size_t i_mesh = 0; i_mesh < size_t(values.path_constraints.cols());
         ++i_mesh) {

        stream << std::setw(4) << i_mesh << "  ";
        for (size_t i_pc = 0; i_pc < pathcon_names.size(); ++i_pc) {
            auto& value = static_cast<const double&>(
                    values.path_constraints(i_pc, i_mesh));
            stream << std::setprecision(2) << std::scientific << std::setw(9)
                    << value << "  ";
        }
        stream << std::endl;
    }

    // Reset the IO format back to what it was before invoking this function.
    stream.copyfmt(orig_fmt);
}

template<typename T>
template<typename S>
typename Trapezoidal<T>::template TrajectoryViewConst<S>
Trapezoidal<T>::make_states_trajectory_view(const VectorX<S>& x) const
{
    // TODO move time variables to the end.
    return {
            // Pointer to the start of the states.
            x.data() + m_num_time_variables,
            m_num_states,      // Number of rows.
            m_num_mesh_points, // Number of columns.
            // Distance between the start of each column.
            Eigen::OuterStride<Eigen::Dynamic>(m_num_states + m_num_controls)};
}

template<typename T>
template<typename S>
typename Trapezoidal<T>::template TrajectoryViewConst<S>
Trapezoidal<T>::make_controls_trajectory_view(const VectorX<S>& x) const
{
    return {
            // Start of controls for first tropter interval.
            x.data() + m_num_time_variables + m_num_states,
            m_num_controls,          // Number of rows.
            m_num_mesh_points,       // Number of columns.
            // Distance between the start of each column; same as above.
            Eigen::OuterStride<Eigen::Dynamic>(m_num_states + m_num_controls)};
}

// TODO avoid the duplication with the above.
template<typename T>
template<typename S>
typename Trapezoidal<T>::template TrajectoryView<S>
Trapezoidal<T>::make_states_trajectory_view(VectorX<S>& x) const
{
    return {
            // Pointer to the start of the states.
            x.data() + m_num_time_variables,
            m_num_states,      // Number of rows.
            m_num_mesh_points, // Number of columns.
            // Distance between the start of each column.
            Eigen::OuterStride<Eigen::Dynamic>(m_num_states + m_num_controls)};
}

template<typename T>
template<typename S>
typename Trapezoidal<T>::template TrajectoryView<S>
Trapezoidal<T>::make_controls_trajectory_view(VectorX<S>& x) const
{
    return {
            // Start of controls for first tropter interval.
            x.data() + m_num_time_variables + m_num_states,
            m_num_controls,          // Number of rows.
            m_num_mesh_points,       // Number of columns.
            // Distance between the start of each column; same as above.
            Eigen::OuterStride<Eigen::Dynamic>(m_num_states + m_num_controls)};
}

template<typename T>
typename Trapezoidal<T>::ConstraintsView
Trapezoidal<T>::make_constraints_view(Eigen::Ref<VectorX<T>> constr) const
{
    // Starting indices of different parts of the constraints vector.
    const unsigned d  = 0;                               // defects.
    T* pc_ptr= m_num_path_constraints ?                  // path constraints.
               &constr[d + m_num_dynamics_constraints] : nullptr;
    //const unsigned pc =  // path
    // constraints.
    return {DefectsTrajectoryView(&constr[d], m_num_states, m_num_defects),
            PathConstraintsTrajectoryView(pc_ptr, m_num_path_constraints,
                    m_num_mesh_points)};
}

} // namespace transcription
} // namespace tropter

#endif // MUSCOLLO_TRANSCRIPTION_TRAPEZOIDAL_HPP
