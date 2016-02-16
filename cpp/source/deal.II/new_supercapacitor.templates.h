/* Copyright (c) 2016, the Cap authors.
 *
 * This file is subject to the Modified BSD License and may not be distributed
 * without copyright and license information. Please refer to the file LICENSE
 * for the text and further information on this license. 
 */

#ifndef CAP_DEAL_II_NEW_SUPERCAPACITOR_TEMPLATES_H
#define CAP_DEAL_II_NEW_SUPERCAPACITOR_TEMPLATES_H

#include <cap/deal.II/new_supercapacitor.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/data_out.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/solver_cg.h>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/floating_point_comparison.hpp>
#include <tuple>
#include <fstream>

namespace cap
{
template <int dim>
New_SuperCapacitor<dim>::New_SuperCapacitor(boost::mpi::communicator const &comm,
                                    boost::property_tree::ptree const &ptree)
    : EnergyStorageDevice(comm)
{
  // get database
  std::shared_ptr<boost::property_tree::ptree const> database =
      std::make_shared<boost::property_tree::ptree>(ptree);

  // build triangulation
  std::shared_ptr<boost::property_tree::ptree> geometry_database =
      std::make_shared<boost::property_tree::ptree>(
          database->get_child("geometry"));
  geometry =
      std::make_shared<cap::SuperCapacitorGeometry<dim>>(geometry_database);
  std::shared_ptr<dealii::Triangulation<dim> const> triangulation =
      geometry->get_triangulation();

  // get data tolerance and maximum number of iterations for the CG solver
  std::shared_ptr<boost::property_tree::ptree> solver_database =
      std::make_shared<boost::property_tree::ptree>(
          database->get_child("solver"));
  max_iter = solver_database->get<unsigned int>("max_iter", 1000);
  rel_tolerance = solver_database->get<double>("rel_tolerance", 1e-12);
  abs_tolerance = solver_database->get<double>("abs_tolerance", 1e-12);
  verbose_lvl = solver_database->get<unsigned int>("verbosity", 0);

  // distribute degrees of freedom
  fe          = std::make_shared<dealii::FESystem<dim>>(dealii::FE_Q<dim>(1), 3);
  dof_handler = std::make_shared<dealii::DoFHandler<dim>>(*triangulation);
  dof_handler->distribute_dofs(*fe);
  
  // Renumber the degrees of freedom component-wise.
  dealii::DoFRenumbering::component_wise(*dof_handler);
  unsigned int const n_components = dealii::DoFTools::n_components(*dof_handler);
  std::vector<dealii::types::global_dof_index> dofs_per_component(n_components);
  dealii::DoFTools::count_dofs_per_component(*dof_handler,
                                             dofs_per_component);

  // clang-format off
  unsigned int const temperature_component      = database->get<unsigned int>("temperature_component");
  unsigned int const solid_potential_component  = database->get<unsigned int>("solid_potential_component");
  unsigned int const liquid_potential_component = database->get<unsigned int>("liquid_potential_component");
  unsigned int const thermal_block              = database->get<unsigned int>("thermal_block");
  unsigned int const electrochemical_block      = database->get<unsigned int>("electrochemical_block");
  unsigned int const n_blocks                   = database->get<unsigned int>("n_blocks");
  // clang-format on
  dealii::types::global_dof_index const n_electrochemical_dofs =
      dofs_per_component[solid_potential_component] +
      dofs_per_component[liquid_potential_component];
  // For now, we have just one block so we can ignore some variable.
  std::ignore = temperature_component;
  std::ignore = thermal_block;
  std::ignore = electrochemical_block;
  std::ignore = n_blocks;

  // read material properties
  std::shared_ptr<boost::property_tree::ptree> material_properties_database =
      std::make_shared<boost::property_tree::ptree>(
          database->get_child("material_properties"));
  MPValuesParameters<dim> params(material_properties_database);
  params.geometry = geometry;
  std::shared_ptr<MPValues<dim>> mp_values =
      std::make_shared<MPValues<dim>>(params);

  std::shared_ptr<boost::property_tree::ptree> boundary_values_database =
      std::make_shared<boost::property_tree::ptree>(
          database->get_child("boundary_values"));
  std::shared_ptr<SuperCapacitorBoundaryValues<dim>> boundary_values =
      std::make_shared<SuperCapacitorBoundaryValues<dim>>(
          SuperCapacitorBoundaryValuesParameters<dim>(
              boundary_values_database));

  // Initialize the electrochemical physics parameters
  electrochemical_physics_params.reset(new ElectrochemicalPhysicsParameters<dim> 
      (database));
  electrochemical_physics_params->dof_handler = dof_handler;
  electrochemical_physics_params->n_dofs = n_electrochemical_dofs;
  electrochemical_physics_params->mp_values =
      std::dynamic_pointer_cast<MPValues<dim> const>(mp_values);
  electrochemical_physics_params->boundary_values =
      std::dynamic_pointer_cast<BoundaryValues<dim> const>(boundary_values);

  electrochemical_physics.reset(
      new ElectrochemicalPhysics<dim>(electrochemical_physics_params));

  // Initialize the size solution
  // Temporary keep using a BlockVector because of PostProcessor.
  solution.reinit(1);
  solution.block(0).reinit(electrochemical_physics->get_system_rhs());

  // initialize postprocessor
  post_processor_params.reset(new SuperCapacitorPostprocessorParameters<dim>(database));
  post_processor_params->dof_handler = dof_handler;
  post_processor_params->solution = std::make_shared<dealii::BlockVector<double>>(solution);
  post_processor_params->mp_values =
      std::dynamic_pointer_cast<MPValues<dim> const>(mp_values);
  post_processor_params->boundary_values =
      std::dynamic_pointer_cast<BoundaryValues<dim> const>(boundary_values);
  post_processor = std::make_shared<SuperCapacitorPostprocessor<dim>>(
      post_processor_params);

  post_processor->reset(post_processor_params);
}

template <int dim>
void New_SuperCapacitor<dim>::inspect(EnergyStorageDeviceInspector *inspector)
{
  inspector->inspect(this);
}

template <int dim>
void New_SuperCapacitor<dim>::print_data(std::ostream &os) const
{
  std::ignore = os;
  std::runtime_error("This function is not implemented");
}

template <int dim>
void New_SuperCapacitor<dim>::get_voltage(double &voltage) const
{
  post_processor->get("voltage", voltage);
}

template <int dim>
void New_SuperCapacitor<dim>::get_current(double &current) const
{
  post_processor->get("current", current);
}

template <int dim>
void New_SuperCapacitor<dim>::reset_voltage(double const voltage)
{
  std::ignore = voltage;
  throw std::runtime_error("This function is not implemented");
}

template <int dim>
void New_SuperCapacitor<dim>::reset_current(double const current)
{
  std::ignore = current;
  throw std::runtime_error("not implemented");
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step_constant_current(
    double const time_step, double const current)
{
  double surface_area = 0.0;
  post_processor->get("surface_area", surface_area);
  electrochemical_physics_params->constant_current_density =
      current / surface_area;
  evolve_one_time_step(time_step, ConstantCurrent);
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step_constant_voltage(
    double const time_step, double const voltage)
{
  electrochemical_physics_params->constant_voltage = voltage;
  evolve_one_time_step(time_step, ConstantLoad);
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step_constant_power(
    double const time_step, double const power)
{
  double surface_area(0.0);
  post_processor->get("surface_area", surface_area);
  dealii::Vector<double> old_solution(solution.block(0));
  int const max_iterations       = 10;
  double const percent_tolerance = 1.0e-2;
  double current(0.0);
  double voltage(0.0);
  get_voltage(voltage);
  for (int k = 0; k < max_iterations; ++k)
  {
    current = power / voltage;
    electrochemical_physics_params->constant_current_density =
        current / surface_area;
    evolve_one_time_step(time_step, ConstantCurrent);
    get_voltage(voltage);
    if (std::abs(power - voltage * current) / std::abs(power) <
        percent_tolerance)
      return;
    solution.block(0) = old_solution;
  }
  throw std::runtime_error("fixed point iteration did not converge in " +
                           std::to_string(max_iterations) + " iterations");
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step_constant_load(
    double const time_step, double const load)
{
  double surface_area;
  post_processor->get("surface_area", surface_area);
  electrochemical_physics_params->constant_load_density =
      load * surface_area;
  evolve_one_time_step(time_step, ConstantLoad);
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step_linear_current(
    double const time_step, double const current)
{
  // TODO: this is a temporary solution
  this->evolve_one_time_step_constant_current(time_step, current);
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step_linear_voltage(
    double const time_step, double const voltage)
{
  // TODO: this is a temporary solution
  this->evolve_one_time_step_constant_voltage(time_step, voltage);
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step_linear_power(
    double const time_step, double const power)
{
  std::ignore = time_step;
  std::ignore = power;

  throw std::runtime_error("THis function is not implemented");
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step_linear_load(
    double const time_step, double const load)
{
  std::ignore = time_step;
  std::ignore = load;

  throw std::runtime_error("This function is not implemented");
}

template <int dim>
void New_SuperCapacitor<dim>::evolve_one_time_step(double const time_step,
    ChargeType charge_type)
{
  // Rebuild the system if necessary
  if ((std::abs(time_step/electrochemical_physics_params->time_step - 1.0)>1e-14) ||
      (charge_type != electrochemical_physics_params->charge_type)) 
  {
    electrochemical_physics_params->time_step = time_step;
    electrochemical_physics_params->charge_type = charge_type;
    electrochemical_physics.reset(
        new ElectrochemicalPhysics<dim>(electrochemical_physics_params));
  }

  // Get the system from the ElectrochemicalPhysiscs object.
  dealii::SparseMatrix<double> const& system_matrix = 
    electrochemical_physics->get_system_matrix();
  dealii::SparseMatrix<double> const& mass_matrix = 
    electrochemical_physics->get_mass_matrix();
  dealii::ConstraintMatrix const& constraint_matrix =
    electrochemical_physics->get_constraint_matrix();
  dealii::Vector<double> const &system_rhs = 
    electrochemical_physics->get_system_rhs();
  dealii::Vector<double> time_dep_rhs = system_rhs;
  time_dep_rhs *= time_step;
  mass_matrix.vmult_add(time_dep_rhs, solution.block(0));


  // Solve the system 
  dealii::deallog.depth_console(verbose_lvl);
  double tolerance = std::max(abs_tolerance,rel_tolerance *
      system_rhs.l2_norm());
  dealii::SolverControl solver_control(max_iter, tolerance);
  dealii::SolverCG<> solver(solver_control);
  // Temporary: no preconditioner so we don't need to take care of the null
  // space. This can be fixed using hp::DoFHandler or adding the null space.
  dealii::PreconditionIdentity preconditioner;
  solver.solve(system_matrix, solution.block(0), time_dep_rhs, preconditioner);
  constraint_matrix.distribute(solution.block(0));
  // Turn off the verbosity of deal.II
  dealii::deallog.depth_console(0);

  (*this->post_processor).reset(this->post_processor_params);
}
} // end namespace cap

#endif
