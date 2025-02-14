/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Libmemcached library
 *
 *  Copyright (C) 2011 Data Differential, http://datadifferential.com/
 *  Copyright (C) 2006-2009 Brian Aker All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *      * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following disclaimer
 *  in the documentation and/or other materials provided with the
 *  distribution.
 *
 *      * The names of its contributors may not be used to endorse or
 *  promote products derived from this software without specific prior
 *  written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */


#include <config.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <memory>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>

#include <libmemcached/memcached.h>

#include "client_options.h"
#include "utilities.h"
#include "generator.h"
#include "execute.h"

#define DEFAULT_INITIAL_LOAD 10000
#define DEFAULT_EXECUTE_NUMBER 10000
#define DEFAULT_CONCURRENCY 1

#define PROGRAM_NAME "memslap"
#define PROGRAM_DESCRIPTION "Generates a load against a memcached custer of servers."
/** +EDIT */
const int cpu_map[] =
{
  0, 4, 8, 12, 16, 20, 24, 28, 32, 36,
  1, 5, 9, 13, 17, 21, 25, 29, 33, 37,
  3, 7, 11, 15, 19, 23, 27, 31, 35, 39,
  2, 6, 10, 14, 18, 22, 26, 30, 34, 38
//  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39
} ;
 /* 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44,
  3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47,
  2, 6, 10, 14, 18, 22, 26, 30, 34, 38, 42, 46,
  1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45*/
/** -EDIT */

/* Global Thread counter */
volatile unsigned int master_wakeup;
pthread_mutex_t sleeper_mutex;
pthread_cond_t sleep_threshhold;

/* Types */
enum test_t {
  SET_TEST,
  GET_TEST,
  MGET_TEST
};

struct thread_context_st {
  unsigned int key_count;
  pairs_st *initial_pairs;
  unsigned int initial_number;
  pairs_st *execute_pairs;
  unsigned int execute_number;
  char **keys;
  size_t *key_lengths;
  test_t test;
  memcached_st *memc;
  const memcached_st* root;
/** +EDIT */
  int id;
/** -EDIT */

  thread_context_st(const memcached_st* memc_arg, test_t test_arg) :
    key_count(0),
    initial_pairs(NULL),
    initial_number(0),
    execute_pairs(NULL),
    execute_number(0),
    keys(0),
    key_lengths(NULL),
    test(test_arg),
    memc(NULL),
    root(memc_arg)
  {
  }

  void init()
  {
    memc= memcached_clone(NULL, root);
  }

  ~thread_context_st()
  {
    if (execute_pairs)
    {
      pairs_free(execute_pairs);
    }
    memcached_free(memc);
  }
};

struct conclusions_st {
  long int load_time;
  long int read_time;
  unsigned int rows_loaded;
  unsigned int rows_read;

  conclusions_st() :
    load_time(0),
    read_time(0),
    rows_loaded(0),
    rows_read()
  { }
};

/* Prototypes */
void options_parse(int argc, char *argv[]);
void conclusions_print(conclusions_st *conclusion);
void scheduler(memcached_server_st *servers, conclusions_st *conclusion);
pairs_st *load_create_data(memcached_st *memc, unsigned int number_of,
                           unsigned int *actual_loaded);
void flush_all(memcached_st *memc);

static bool opt_binary= 0;
static int opt_verbose= 0;
static int opt_flush= 0;
static int opt_non_blocking_io= 0;
static int opt_tcp_nodelay= 0;
static unsigned int opt_execute_number= 0;
static unsigned int opt_createial_load= 0;
static unsigned int opt_concurrency= 0;
static int opt_displayflag= 0;
static char *opt_servers= NULL;
static int opt_udp_io= 0;
test_t opt_test= SET_TEST;

extern "C" {

static void *run_task(void *p)
{
  thread_context_st *context= (thread_context_st *)p;
/** +EDIT */
/*
  cpu_set_t cpu_set;

  int real_id = cpu_map[25 + (context->id % 23)];
  CPU_ZERO(&cpu_set);
  CPU_SET(real_id, &cpu_set);

  if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) < 0)
  {
    fprintf(stderr, "WARNING: Could not set CPU Affinity, continuing...\n");
  }
*/
// printf("Thread %d bound to core %d.\n", context->id, real_id);
/** -EDIT */

  context->init();

  pthread_mutex_lock(&sleeper_mutex);
  while (master_wakeup)
  {
    pthread_cond_wait(&sleep_threshhold, &sleeper_mutex);
  }
  pthread_mutex_unlock(&sleeper_mutex);

  /* Do Stuff */
  switch (context->test)
  {
  case SET_TEST:
    assert(context->execute_pairs);
    execute_set(context->memc, context->execute_pairs, context->execute_number);
    break;

  case GET_TEST:
    execute_get(context->memc, context->initial_pairs, context->initial_number);
    break;

  case MGET_TEST:
    execute_mget(context->memc, (const char*const*)context->keys, context->key_lengths, context->initial_number);
    break;
  }

  delete context;

  pthread_exit(0);
}

}


int main(int argc, char *argv[])
{
  conclusions_st conclusion;
  /** +EDIT */
  int i;

  cpu_set_t cpu_set;

  CPU_ZERO(&cpu_set);
/*
  CPU_SET(cpu_map[24], &cpu_set);

  if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) < 0)
    fprintf(stderr, "[BENCHMARK] Error: pthread_setaffinity_np\n");
*/
  for (i = 0; i < 20; i++)
        CPU_CLR(cpu_map[i], &cpu_set);

  for (i = 20; i < 40; i++)
        CPU_SET(cpu_map[i], &cpu_set);

  if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) < 0)
    fprintf(stderr, "[BENCHMARK] Error: sched_setaffinity\n");
  /** -EDIT */

  srandom((unsigned int)time(NULL));
  options_parse(argc, argv);

  if (opt_servers == NULL)
  {
    char *temp;

    if ((temp= getenv("MEMCACHED_SERVERS")))
    {
      opt_servers= strdup(temp);
    }
    else
    {
      fprintf(stderr, "No Servers provided\n");
      return EXIT_FAILURE;
    }
  }

  memcached_server_st *servers= memcached_servers_parse(opt_servers);

  pthread_mutex_init(&sleeper_mutex, NULL);
  pthread_cond_init(&sleep_threshhold, NULL);

  scheduler(servers, &conclusion);

  free(opt_servers);

  (void)pthread_mutex_destroy(&sleeper_mutex);
  (void)pthread_cond_destroy(&sleep_threshhold);
  conclusions_print(&conclusion);
  memcached_server_list_free(servers);

  return EXIT_SUCCESS;
}

void scheduler(memcached_server_st *servers, conclusions_st *conclusion)
{
  unsigned int actual_loaded= 0; /* Fix warning */

  struct timeval start_time, end_time;
  pairs_st *pairs= NULL;

  memcached_st *memc= memcached_create(NULL);

  /* We need to set udp behavior before adding servers to the client */
  if (opt_udp_io)
  {
    memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_USE_UDP,
                           (uint64_t)opt_udp_io);
    for (uint32_t x= 0; x < memcached_server_list_count(servers); x++ )
    {
      servers[x].type= MEMCACHED_CONNECTION_UDP;
    }
  }
  memcached_server_push(memc, servers);

  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL,
                         (uint64_t)opt_binary);

  if (opt_flush)
    flush_all(memc);

  if (opt_createial_load)
    pairs= load_create_data(memc, opt_createial_load, &actual_loaded);

  char **keys= static_cast<char **>(calloc(actual_loaded, sizeof(char*)));
  size_t *key_lengths= static_cast<size_t *>(calloc(actual_loaded, sizeof(size_t)));

  if (keys == NULL or key_lengths == NULL)
  {
    free(keys);
    free(key_lengths);
    keys= NULL;
    key_lengths= NULL;
  }
  else
  {
    for (uint32_t x= 0; x < actual_loaded; ++x)
    {
      keys[x]= pairs[x].key;
      key_lengths[x]= pairs[x].key_length;
    }
  }

  /* We set this after we have loaded */
  {
    if (opt_non_blocking_io)
      memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_NO_BLOCK, 1);

    if (opt_tcp_nodelay)
      memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_TCP_NODELAY, 1);
  }

  pthread_mutex_lock(&sleeper_mutex);
  master_wakeup= 1;
  pthread_mutex_unlock(&sleeper_mutex);

  pthread_t *threads= new  (std::nothrow) pthread_t[opt_concurrency];

  if (not threads)
  {
    exit(EXIT_FAILURE);
  }

  for (uint32_t x= 0; x < opt_concurrency; x++)
  {
    thread_context_st *context= new thread_context_st(memc, opt_test);
    context->test= opt_test;

    context->initial_pairs= pairs;
    context->initial_number= actual_loaded;
    context->keys= keys;
    context->key_lengths= key_lengths;
/** +EDIT */                                                                     
    context->id = x;                                                             
/** -EDIT */

    if (opt_test == SET_TEST)
    {
      context->execute_pairs= pairs_generate(opt_execute_number, 400);
      context->execute_number= opt_execute_number;
    }

    /* now you create the thread */
    if (pthread_create(threads +x, NULL, run_task, (void *)context) != 0)
    {
      fprintf(stderr,"Could not create thread\n");
      exit(1);
    }
  }

  pthread_mutex_lock(&sleeper_mutex);
  master_wakeup= 0;
  pthread_mutex_unlock(&sleeper_mutex);
  pthread_cond_broadcast(&sleep_threshhold);
  gettimeofday(&start_time, NULL);

  for (uint32_t x= 0; x < opt_concurrency; x++)
  {
    void *retval;
    pthread_join(threads[x], &retval);
  }
  delete [] threads;

  gettimeofday(&end_time, NULL);

  conclusion->load_time= timedif(end_time, start_time);
  conclusion->read_time= timedif(end_time, start_time);
  free(keys);
  free(key_lengths);
  pairs_free(pairs);
  memcached_free(memc);
}

void options_parse(int argc, char *argv[])
{
  memcached_programs_help_st help_options[]=
  {
    {0},
  };

  static struct option long_options[]=
    {
      {(OPTIONSTRING)"concurrency", required_argument, NULL, OPT_SLAP_CONCURRENCY},
      {(OPTIONSTRING)"debug", no_argument, &opt_verbose, OPT_DEBUG},
      {(OPTIONSTRING)"quiet", no_argument, NULL, OPT_QUIET},
      {(OPTIONSTRING)"execute-number", required_argument, NULL, OPT_SLAP_EXECUTE_NUMBER},
      {(OPTIONSTRING)"flag", no_argument, &opt_displayflag, OPT_FLAG},
      {(OPTIONSTRING)"flush", no_argument, &opt_flush, OPT_FLUSH},
      {(OPTIONSTRING)"help", no_argument, NULL, OPT_HELP},
      {(OPTIONSTRING)"initial-load", required_argument, NULL, OPT_SLAP_INITIAL_LOAD}, /* Number to load initially */
      {(OPTIONSTRING)"non-blocking", no_argument, &opt_non_blocking_io, OPT_SLAP_NON_BLOCK},
      {(OPTIONSTRING)"servers", required_argument, NULL, OPT_SERVERS},
      {(OPTIONSTRING)"tcp-nodelay", no_argument, &opt_tcp_nodelay, OPT_SLAP_TCP_NODELAY},
      {(OPTIONSTRING)"test", required_argument, NULL, OPT_SLAP_TEST},
      {(OPTIONSTRING)"verbose", no_argument, &opt_verbose, OPT_VERBOSE},
      {(OPTIONSTRING)"version", no_argument, NULL, OPT_VERSION},
      {(OPTIONSTRING)"binary", no_argument, NULL, OPT_BINARY},
      {(OPTIONSTRING)"udp", no_argument, NULL, OPT_UDP},
      {0, 0, 0, 0},
    };

  bool opt_help= false;
  bool opt_version= false;
  int option_index= 0;
  while (1)
  {
    int option_rv= getopt_long(argc, argv, "Vhvds:", long_options, &option_index);

    if (option_rv == -1) break;

    switch (option_rv)
    {
    case 0:
      break;

    case OPT_UDP:
      if (opt_test == GET_TEST)
      {
        fprintf(stderr, "You can not run a get test in UDP mode. UDP mode "
                  "does not currently support get ops.\n");
        exit(1);
      }
      opt_udp_io= 1;
      break;

    case OPT_BINARY:
      opt_binary= true;
      break;

    case OPT_VERBOSE: /* --verbose or -v */
      opt_verbose= OPT_VERBOSE;
      break;

    case OPT_DEBUG: /* --debug or -d */
      opt_verbose = OPT_DEBUG;
      break;

    case OPT_VERSION: /* --version or -V */
      opt_version= true;
      break;

    case OPT_HELP: /* --help or -h */
      opt_help= true;
      break;

    case OPT_SERVERS: /* --servers or -s */
      opt_servers= strdup(optarg);
      break;

    case OPT_SLAP_TEST:
      if (strcmp(optarg, "get") == 0)
      {
        if (opt_udp_io == 1)
        {
          fprintf(stderr, "You can not run a get test in UDP mode. UDP mode "
                  "does not currently support get ops.\n");
          exit(EXIT_FAILURE);
        }
        opt_test= GET_TEST ;
      }
      else if (strcmp(optarg, "set") == 0)
      {
        opt_test= SET_TEST;
      }
      else if (strcmp(optarg, "mget") == 0)
      {
        opt_test= MGET_TEST;
      }
      else
      {
        fprintf(stderr, "Your test, %s, is not a known test\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;

    case OPT_SLAP_CONCURRENCY:
      opt_concurrency= (unsigned int)strtoul(optarg, (char **)NULL, 10);
      break;

    case OPT_SLAP_EXECUTE_NUMBER:
      opt_execute_number= (unsigned int)strtoul(optarg, (char **)NULL, 10);
      break;

    case OPT_SLAP_INITIAL_LOAD:
      opt_createial_load= (unsigned int)strtoul(optarg, (char **)NULL, 10);
      break;

    case OPT_QUIET:
      close_stdio();
      break;


    case '?':
      /* getopt_long already printed an error message. */
      exit(EXIT_FAILURE);

    default:
      abort();
    }
  }

  if (opt_version)
  {
    version_command(PROGRAM_NAME);
    exit(EXIT_SUCCESS);
  }

  if (opt_help)
  {
    help_command(PROGRAM_NAME, PROGRAM_DESCRIPTION, long_options, help_options);
    exit(EXIT_SUCCESS);
  }

  if ((opt_test == GET_TEST or opt_test == MGET_TEST) and opt_createial_load == 0)
    opt_createial_load= DEFAULT_INITIAL_LOAD;

  if (opt_execute_number == 0)
    opt_execute_number= DEFAULT_EXECUTE_NUMBER;

  if (opt_concurrency == 0)
    opt_concurrency= DEFAULT_CONCURRENCY;
}

void conclusions_print(conclusions_st *conclusion)
{
  printf("\tThreads connecting to servers %u\n", opt_concurrency);
#ifdef NOT_FINISHED
  printf("\tLoaded %u rows\n", conclusion->rows_loaded);
  printf("\tRead %u rows\n", conclusion->rows_read);
#endif
  if (opt_test == SET_TEST)
    printf("\tTook %ld.%03ld seconds to load data\n", conclusion->load_time / 1000,
           conclusion->load_time % 1000);
  else
    printf("\tTook %ld.%03ld seconds to read data\n", conclusion->read_time / 1000,
           conclusion->read_time % 1000);
}

void flush_all(memcached_st *memc)
{
  memcached_flush(memc, 0);
}

pairs_st *load_create_data(memcached_st *memc, unsigned int number_of,
                           unsigned int *actual_loaded)
{
  memcached_st *memc_clone= memcached_clone(NULL, memc);
  /* We always used non-blocking IO for load since it is faster */
  memcached_behavior_set(memc_clone, MEMCACHED_BEHAVIOR_NO_BLOCK, 0);

  pairs_st *pairs= pairs_generate(number_of, 400);
  *actual_loaded= execute_set(memc_clone, pairs, number_of);

  memcached_free(memc_clone);

  return pairs;
}
