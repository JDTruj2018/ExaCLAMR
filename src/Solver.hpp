/**
 * @file
 * @author Patrick Bridges <pbridges@unm.edu>
 * @author Jered Dominguez-Trujillo <jereddt@unm.edu>
 * 
 * @section DESCRIPTION
 * 
 */
 
#ifndef EXACLAMR_SOLVER_HPP
#define EXACLAMR_SOLVER_HPP

#ifndef DEBUG
#define DEBUG 0 
#endif

#include <Mesh.hpp>
#include <ProblemManager.hpp>
#include <TimeIntegration.hpp>
#include <Timer.hpp>

#ifdef HAVE_SILO
    #include <SiloWriter.hpp>
#endif

#include <Cajita.hpp>
#include <Kokkos_Core.hpp>

// TODO: ifdef Include MPI
#include <mpi.h>

#include <memory>

#define MICROSECONDS 1.0e-6


namespace ExaCLAMR
{


template <typename state_t>
class SolverBase {
    public:
        virtual ~SolverBase() = default;
        virtual void solve( const int write_freq ) = 0;
        virtual state_t timeCompute() = 0;
        virtual state_t timeCommunicate() = 0;
};


template <class MemorySpace, class ExecutionSpace, typename state_t>
class Solver : public SolverBase<state_t> {
    using timestruct = std::chrono::high_resolution_clock::time_point;

    public:
        /**
         * Constructor
         * 
         * @param
         */
        template <class InitFunc>
        Solver( MPI_Comm comm, 
                const InitFunc& create_functor,
                const std::array<state_t, 6>& global_bounding_box, 
                const std::array<int, 3>& global_num_cell, 
                const std::array<bool, 3>& periodic,
                const Cajita::Partitioner& partitioner,
                const int halo_size, 
                const int time_steps, 
                const state_t gravity )
        : _halo_size ( halo_size ), _time_steps ( time_steps ), _gravity ( gravity ), _time_compute ( 0.0 ), _time_communicate ( 0.0 ) {
            // TODO: ifdef MPI Comm Rank Statement
            MPI_Comm_rank( comm, &_rank );

            if ( _rank == 0 && DEBUG ) std::cout << "Created Solver\n";         // DEBUG: Trace Created Solver
                
            // Create Problem Manager
            _pm = std::make_shared<ProblemManager<MemorySpace, ExecutionSpace, state_t>>( 
                                            global_bounding_box, 
                                            global_num_cell, 
                                            periodic, 
                                            partitioner, 
                                            halo_size, 
                                            comm, 
                                            create_functor );

            // Create Silo Writer
            #ifdef HAVE_SILO
                _silo = std::make_shared<SiloWriter<MemorySpace, ExecutionSpace, state_t>>( _pm );
            #endif

            // TODO: ifdef MPI Barrier Statement
            MPI_Barrier( MPI_COMM_WORLD );
        };

        // Toggle Between Current and New State Vectors
        #define NEWFIELD( time_step ) ( ( time_step + 1 ) % 2 )
        #define CURRENTFIELD( time_step ) ( ( time_step ) % 2 )

        // Print Output of Height Array to Console for Debugging
        void output( const int rank, const int time_step, const state_t current_time, const state_t dt ) {
            // Get Domain Iteration Space
            auto domain = _pm->mesh()->domainSpace();                                                       // Domain Space to Iterate Over

            // Get State Views
            auto hNew = _pm->get( Location::Cell(), Field::Height(), NEWFIELD( time_step ) );               // New Height State View
            auto uNew = _pm->get( Location::Cell(), Field::Velocity(), NEWFIELD( time_step ) );             // New Velocity State View

            state_t summedHeight = 0;                                                                       // Initialize Total Height

            // Only Loop if Rank is the Specified Rank
            if ( _pm->mesh()->rank() == rank ) {
                for ( int i = domain.min( 0 ); i < domain.max( 0 ); i++ ) {
                    for ( int j = domain.min( 1 ); j < domain.max( 1 ); j++ ) {
                        for ( int k = domain.min( 2 ); k < domain.max( 2 ); k++ ) {
                            if ( DEBUG ) std::cout << std::left << std::setw(8) << hNew( i, j, k, 0 );      // DEBUG: Print Height Array
                            summedHeight += hNew( i, j, k, 0 );                                             // Sum Heights as a Proxy for Mass
                        }
                    }
                    if( DEBUG ) std::cout << "\n";                                                          // DEBUG: New Line
                }

                // Proxy Mass Conservation
                if ( DEBUG ) std::cout << "Summed Height: " << summedHeight << "\n";                        // DEBUG: Print Summed Height
            }
        };

        // Solve Routine
        void solve( const int write_freq ) override {
            if ( _rank == 0 && DEBUG ) std::cout << "Solving!\n";       // DEBUG: Trace Solving

            int time_step = 0;
            int nt = _time_steps;
            state_t current_time = 0.0, mindt = 0.0;

            // Rank 0 Prints Initial Iteration and Time
            if (_rank == 0 ) {
                std::cout << std::left << std::setw(12) << "Iteration: " << 0 <<                            // Print Iteration and Current Time
                std::left << std::setw(15) << "\tCurrent Time: " << current_time << "\n";                   
                if ( DEBUG ) output( 0, time_step, current_time, mindt );                                   // DEBUG: Call Output Routine
            }

            // Write Initial Data to File with Silo
            #ifdef HAVE_SILO
                _silo->siloWrite( strdup( "Mesh" ), 0, current_time, mindt );                                // Write State Data
            #endif

            // Loop Over Time
            for (time_step = 1; time_step <= nt; time_step++) {
                // TODO: Declare Sigma = 0.95 elsewhere instead of hard-coded
                state_t dt = TimeIntegrator::setTimeStep( *_pm, ExecutionSpace(), MemorySpace(), _gravity, 0.95, time_step );       // Calculate Time Step

                // TODO: ifdef MPI AllReduce Statement
                // TODO: Scenario where we need MPI_FLOAT
                MPI_Allreduce( &dt, &mindt, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD );                                               // Get Minimum Time Step

                Timer::timer_start( &_timer_compute );                                                                              // Start Timer
                TimeIntegrator::step( *_pm, ExecutionSpace(), MemorySpace(), mindt, _gravity, time_step );                          // Perform Calculation
                _time_compute += ( state_t ) Timer::timer_stop( _timer_compute ) * MICROSECONDS;                                    // Stop Timer, Increment Time

                Timer::timer_start( &_timer_communicate );                                                                           // Start Timer
                TimeIntegrator::haloExchange( *_pm, time_step );                                                                    // Perform Communication
                _time_communicate += ( state_t ) Timer::timer_stop( _timer_communicate ) * MICROSECONDS;                            // Stop Timer, Increment Time

                // Increment Current Time
                current_time += mindt;

                // Output and Write File every Write Frequency Time Steps
                if ( 0 == time_step % write_freq ) {
                    if ( 0 == _rank ) std::cout << std::left << std::setw(12) << "Iteration: " << std::setw(5) << time_step <<      // Print Iteration and Current Time
                    std::left << std::setw(15) << "\tCurrent Time: " << current_time << "\n";

                    if ( DEBUG ) output( 0, time_step, current_time, mindt );                                                       // DEBUG: Call Output Routine

                    // Write Current State Data to File with Silo
                    #ifdef HAVE_SILO
                        _silo->siloWrite( strdup( "Mesh" ), time_step, current_time, mindt );                                       // Write State Data
                    #endif
                }
            }
        };

        state_t timeCompute() {
            return _time_compute;
        };

        state_t timeCommunicate() {
            return _time_communicate;
        };
        

    private:
        int _rank, _time_steps, _halo_size;
        state_t _gravity;
        std::shared_ptr<ProblemManager<MemorySpace, ExecutionSpace, state_t>> _pm;
        #ifdef HAVE_SILO
            std::shared_ptr<SiloWriter<MemorySpace, ExecutionSpace, state_t>> _silo;
        #endif
        timestruct _timer_compute, _timer_communicate;
        state_t _time_compute, _time_communicate;
};

// Create Solver with Templates
template <typename state_t, class InitFunc>
std::shared_ptr<SolverBase<state_t>> createSolver( const std::string& device,
                                            MPI_Comm comm, 
                                            const InitFunc& create_functor,
                                            const std::array<state_t, 6>& global_bounding_box, 
                                            const std::array<int, 3>& global_num_cell,
                                            const std::array<bool, 3>& periodic,
                                            const Cajita::Partitioner& partitioner, 
                                            const int halo_size, 
                                            const int time_steps, 
                                            const state_t gravity ) {
    // Serial
    if ( 0 == device.compare( "serial" ) ) {
        #ifdef KOKKOS_ENABLE_SERIAL
            return std::make_shared<ExaCLAMR::Solver<Kokkos::HostSpace, Kokkos::Serial, state_t>>(
                comm, 
                create_functor,
                global_bounding_box, 
                global_num_cell, 
                periodic,
                partitioner,
                halo_size, 
                time_steps, 
                gravity );
        #else
            throw std::runtime_error( "Serial Backend Not Enabled" );
        #endif
    }
    // OpenMP
    else if ( 0 == device.compare( "openmp" ) ) {
        #ifdef KOKKOS_ENABLE_OPENMP
            return std::make_shared<ExaCLAMR::Solver<Kokkos::HostSpace, Kokkos::OpenMP, state_t>>(
                comm, 
                create_functor,
                global_bounding_box, 
                global_num_cell, 
                periodic,
                partitioner,
                halo_size, 
                time_steps, 
                gravity );
        #else
            throw std::runtime_error( "OpenMP Backend Not Enabled" );
        #endif
    }
    // TODO: Add CUDA
    // Otherwise
    else {
        throw std::runtime_error( "Invalid Backend" );
        return nullptr;
    }
};

}

#endif
