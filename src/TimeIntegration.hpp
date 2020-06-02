#ifndef EXACLAMR_TIMEINTEGRATION_HPP
#define EXACLAMR_TIMEINTEGRATION_HPP

#include <ProblemManager.hpp>

#include <stdio.h>

namespace ExaCLAMR
{
namespace TimeIntegrator
{

template<class ProblemManagerType, class ExecutionSpace>
void applyBoundaryConditions( const ProblemManagerType& pm, const ExecutionSpace& exec_space ) {
    if ( pm.mesh()->rank() == 0 ) printf( "Applying Boundary Conditions\n" );
}

template<class ProblemManagerType>
void haloExchange( const ProblemManagerType& pm, const int a, const int b ) {
    if ( pm.mesh()->rank() == 0 ) printf( "Starting Halo Exchange\ta: %d\t b: %d\n", a, b );

    auto local_grid = pm.mesh()->localGrid();
    auto owned_cells = local_grid->indexSpace( Cajita::Own(), Cajita::Cell(), Cajita::Local() );

    auto uCurrent = pm.get( Location::Cell(), Field::Velocity(), a );
    auto hCurrent = pm.get( Location::Cell(), Field::Height(), a );

    auto uNew = pm.get( Location::Cell(), Field::Velocity(), b );
    auto hNew = pm.get( Location::Cell(), Field::Height(), b );

    for ( int i = -1; i < 2; i++ ) {
        for (int j = -1; j < 2; j++) {
            if ( ( i == 0 || j == 0 ) && !( i == 0 && j == 0 ) ){
                int neighbor = local_grid->neighborRank( i, j, 0 );
                if ( neighbor != -1 ) {
                    auto shared_recv_cells = local_grid->sharedIndexSpace( Cajita::Ghost(), Cajita::Cell(), i, j, 0 );
                    auto shared_send_cells = local_grid->sharedIndexSpace( Cajita::Own(), Cajita::Cell(), i, j, 0 );

                    /*
                    printf( "Rank: %d\t i: %d\tj: %d\tk: %d\t Neighbor: %d\n", pm.mesh()->rank(), i, j, 0, neighbor );
                    printf( "Rank (Recv): %d\txmin: %d\t xmax: %d\tymin: %d\tymax: %d\tzmin: %d\tzmax: %d\tsize: %d\n", pm.mesh()->rank(), \
                    shared_recv_cells.min( 0 ), shared_recv_cells.max( 0 ), shared_recv_cells.min( 1 ), shared_recv_cells.max( 1 ), \
                    shared_recv_cells.min( 2 ), shared_recv_cells.max( 2 ), shared_recv_cells.size() );
                    printf( "Rank (Send): %d\txmin: %d\t xmax: %d\tymin: %d\tymax: %d\tzmin: %d\tzmax: %d\tsize: %d\n", pm.mesh()->rank(), \
                    shared_send_cells.min( 0 ), shared_send_cells.max( 0 ), shared_send_cells.min( 1 ), shared_send_cells.max( 1 ), \
                    shared_send_cells.min( 2 ), shared_send_cells.max( 2 ), shared_send_cells.size() );
                    */


                    double sendH[shared_send_cells.size()];
                    double sendU[shared_send_cells.size()];
                    double sendV[shared_send_cells.size()];
                    double recvH[shared_recv_cells.size()];
                    double recvU[shared_recv_cells.size()];
                    double recvV[shared_recv_cells.size()];

                    for ( int ii = shared_send_cells.min( 0 ); ii < shared_send_cells.max( 0 ); ii++ ) {
                        for ( int jj = shared_send_cells.min( 1 ); jj < shared_send_cells.max( 1 ); jj++ ) {
                            for ( int kk = shared_send_cells.min( 2 ); kk < shared_send_cells.max( 2 ); kk++ ) {
                                int ii_own = ii - shared_send_cells.min( 0 );
                                int jj_own = jj - shared_send_cells.min( 1 );
                                int kk_own = kk - shared_send_cells.min( 2 );

                                int inx = ii_own + shared_send_cells.extent( 0 ) * ( jj_own + shared_send_cells.extent( 1 ) * kk_own );

                                // printf( "ii: %d\tjj: %d\tkk: %d\t ii_own: %d\tjj_own: %d\tkk_own: %d\tinx: %d\n", ii, jj, kk, ii_own, jj_own, kk_own, inx );

                                sendH[inx] = hNew( ii, jj, kk, 0 );
                                sendU[inx] = uNew( ii, jj, kk, 0 );
                                sendV[inx] = uNew( ii, jj, kk, 1 );
                            }
                        }
                    }

                    MPI_Request request[6];
                    MPI_Status statuses[6];

                    MPI_Isend( sendH, shared_send_cells.size(), MPI_DOUBLE, neighbor, 0, MPI_COMM_WORLD, &request[0] );
                    MPI_Isend( sendU, shared_send_cells.size(), MPI_DOUBLE, neighbor, 0, MPI_COMM_WORLD, &request[1] );
                    MPI_Isend( sendV, shared_send_cells.size(), MPI_DOUBLE, neighbor, 0, MPI_COMM_WORLD, &request[2] );

                    MPI_Irecv( recvH, shared_recv_cells.size(), MPI_DOUBLE, neighbor, 0, MPI_COMM_WORLD, &request[3] );
                    MPI_Irecv( recvU, shared_recv_cells.size(), MPI_DOUBLE, neighbor, 0, MPI_COMM_WORLD, &request[4] );
                    MPI_Irecv( recvV, shared_recv_cells.size(), MPI_DOUBLE, neighbor, 0, MPI_COMM_WORLD, &request[5] );

                    MPI_Waitall( 6, request, statuses );
                    
                    for ( int ii = shared_recv_cells.min( 0 ); ii < shared_recv_cells.max( 0 ); ii++ ) {
                        for ( int jj = shared_recv_cells.min( 1 ); jj < shared_recv_cells.max( 1 ); jj++ ) {
                            for ( int kk = shared_recv_cells.min( 2 ); kk < shared_recv_cells.max( 2 ); kk++ ) {
                                int ii_own = ii - shared_recv_cells.min( 0 );
                                int jj_own = jj - shared_recv_cells.min( 1 );
                                int kk_own = kk - shared_recv_cells.min( 2 );

                                int inx = ii_own + shared_recv_cells.extent( 0 ) * ( jj_own + shared_recv_cells.extent( 1 ) * kk_own );

                                // printf( "Rank: %d\tii: %d\tjj: %d\tkk: %d\t ii_own: %d\tjj_own: %d\tkk_own: %d\tinx: %d\trecvH: %.4f\n", \
                                pm.mesh()->rank(), ii, jj, kk, ii_own, jj_own, kk_own, inx, recvH[inx] );

                                hNew( ii, jj, kk, 0 ) = recvH[inx];
                                uNew( ii, jj, kk, 0 ) = recvU[inx];
                                uNew( ii, jj, kk, 1 ) = recvV[inx];
                            }
                        }
                    }
                }
            }
        }
    }

    /*
    if ( pm.mesh()->rank() == 0 ) {
        for ( int i = 0; i < owned_cells.extent( 0 ); i++ ) {
            for ( int j = 0; j < owned_cells.extent( 1 ); j++ ) {
                for ( int k = 0; k < owned_cells.extent( 2 ); k++ ) {
                    printf( "%.4f\t", hNew( i, j, k, 0 ) );
                }
            }
            printf("\n");
        }
    }
    */

    MPI_Barrier( MPI_COMM_WORLD );
}

template<class ProblemManagerType, class ExecutionSpace, class MemorySpace, class state_t>
void step( const ProblemManagerType& pm, const ExecutionSpace& exec_space, const MemorySpace& mem_space, const state_t dt, const state_t gravity, const int tstep ) {
    if ( pm.mesh()->rank() == 0 ) printf( "Time Stepper\n" );

    using device_type = typename Kokkos::Device<ExecutionSpace, MemorySpace>;

    applyBoundaryConditions( pm, exec_space);

    auto local_grid = pm.mesh()->localGrid();
    auto local_mesh = Cajita::createLocalMesh<device_type>( *local_grid );

    auto owned_cells = local_grid->indexSpace( Cajita::Own(), Cajita::Cell(), Cajita::Local() );
    auto global_bounding_box = pm.mesh()->globalBoundingBox();

    int a, b;
    if ( tstep % 2 == 0 ) {
        a = 0;
        b = 1;
    }
    else {
        a = 1;
        b = 0;
    }

    auto uCurrent = pm.get( Location::Cell(), Field::Velocity(), a );
    auto hCurrent = pm.get( Location::Cell(), Field::Height(), a );

    auto uNew = pm.get( Location::Cell(), Field::Velocity(), b );
    auto hNew = pm.get( Location::Cell(), Field::Height(), b );

    Kokkos::parallel_for( Cajita::createExecutionPolicy( owned_cells, exec_space ), KOKKOS_LAMBDA( const int i, const int j, const int k ) {
        int coords[3] = { i, j, k };
        state_t x[3];
        local_mesh.coordinates( Cajita::Cell(), coords, x );

        if ( x[0] >= global_bounding_box[0] && x[1] >= global_bounding_box[1] && x[2] >= global_bounding_box[2] 
        && x[0] <= global_bounding_box[3] && x[1] <= global_bounding_box[4] && x[2] <= global_bounding_box[5] ) {
            /*
            printf( "Rank: %d\tx: %.4f\ty: %.4f\tz: %.4f\n", pm.mesh()->rank(), x[0], x[1], x[2] );
            printf( "Rank: %d\ti: %d\tj: %d\tk: %d\n", pm.mesh()->rank(), i, j, k );
            */

            hNew( i, j, k, 0 ) = ( hCurrent( i - 1, j, k, 0 ) + hCurrent( i + 1, j, k, 0 ) + hCurrent( i, j - 1, k, 0 ) + hCurrent( i, j + 1, k, 0 ) ) / 4;
        }
    } );

    /*
    if ( pm.mesh()->rank() == 0 ) {
        for ( int i = 0; i < owned_cells.extent( 0 ); i++ ) {
            for ( int j = 0; j < owned_cells.extent( 1 ); j++ ) {
                for ( int k = 0; k < owned_cells.extent( 2 ); k++ ) {
                    printf( "%.4f\t", hNew( i, j, k, 0 ) );
                }
            }
            printf("\n");
        }
    }
    */

    MPI_Barrier( MPI_COMM_WORLD );

    haloExchange( pm, a, b );

}

}

}

#endif