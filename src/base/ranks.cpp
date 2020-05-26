#ifdef HAVE_CONFIG_H
 #include "config.hpp"
#endif

#define EXTERN_RANK
 #include "base/ranks.hpp"

namespace ciccios
{
  void initRanks(int& narg,char **&arg)
  {
#ifdef USE_MPI
    int provided;
    MPI_Init_thread(&narg,&arg,MPI_THREAD_SERIALIZED,&provided);
    MPI_Comm_rank(MPI_COMM_WORLD,&resources::rank);
    MPI_Comm_size(MPI_COMM_WORLD,&resources::nRanks);
#endif
  }
  
  void finalizeRanks()
  {
#ifdef USE_MPI
    MPI_Finalize();
#endif
  }
}
