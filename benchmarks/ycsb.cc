#include <iostream>
#include <sstream>
#include <vector>
#include <utility>
#include <string>
#include <set>
#include <iomanip>
#include <cstring>

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <numa.h>

#include "../macros.h"
#include "../varkey.h"
#include "../thread.h"
#include "../util.h"
#include "../spinbarrier.h"
#include "../core.h"

#include "bench.h"

using namespace std;
using namespace util;

static size_t nkeys;
static const size_t YCSBRecordSize = 100;

// [R, W, RMW, Scan]
// we're missing remove for now
// the default is a modification of YCSB "A" we made (80/20 R/W)
static unsigned g_txn_workload_mix[] = {80, 20, 0, 0};

auto get_key(uint64_t wallet_id, uint64_t output_id) -> std::string
{
  auto ret = std::string();
  ret.resize(32, '0');
  auto actual_wallet_id = wallet_id + 1;
  std::memcpy(ret.data(), &actual_wallet_id, sizeof(actual_wallet_id));
  std::memcpy(ret.data() + sizeof(actual_wallet_id), &output_id, sizeof(output_id));
  return ret;
}

auto get_mint_key(uint64_t output_id) -> std::string
{
  auto ret = std::string();
  ret.resize(32, '0');
  std::memcpy(ret.data() + sizeof(output_id), &output_id, sizeof(output_id));
  return ret;
}

class ycsb_worker : public bench_worker
{
public:
  ycsb_worker(unsigned int worker_id,
              unsigned long seed, abstract_db *db,
              const map<string, abstract_ordered_index *> &open_tables,
              spin_barrier *barrier_a, spin_barrier *barrier_b)
      : bench_worker(worker_id, true, seed, db,
                     open_tables, barrier_a, barrier_b),
        tbl(open_tables.at("USERTABLE")),
        computation_n(0)
  {
    obj_key0.reserve(str_arena::MinStrReserveLength);
    obj_key1.reserve(str_arena::MinStrReserveLength);
    obj_v.reserve(str_arena::MinStrReserveLength);

    m_keys_per_worker = nkeys / nthreads;
    m_current_input = worker_id * m_keys_per_worker;
    m_current_output = 0;
    m_current_spent_output = 0;
  }

  txn_result
  txn_read()
  {
    void *const txn = db->new_txn(txn_flags, arena, txn_buf(), abstract_db::HINT_KV_GET_PUT);
    scoped_str_arena s_arena(arena);
    try
    {
      const uint64_t k = r.next() % nkeys;
      ALWAYS_ASSERT(tbl->get(txn, u64_varkey(k).str(obj_key0), obj_v));
      computation_n += obj_v.size();
      measure_txn_counters(txn, "txn_read");
      if (likely(db->commit_txn(txn)))
        return txn_result(true, 0);
    }
    catch (abstract_db::abstract_abort_exception &ex)
    {
      db->abort_txn(txn);
    }
    return txn_result(false, 0);
  }

  static txn_result
  TxnRead(bench_worker *w)
  {
    return static_cast<ycsb_worker *>(w)->txn_read();
  }

  txn_result
  txn_write()
  {
    void *const txn = db->new_txn(txn_flags, arena, txn_buf(), abstract_db::HINT_KV_GET_PUT);
    scoped_str_arena s_arena(arena);
    try
    {
      auto s = u64_varkey(r.next() % nkeys).str(str());
      auto s2 = str().assign(YCSBRecordSize, 'b');
      tbl->put(txn, s, s2);
      measure_txn_counters(txn, "txn_write");
      if (likely(db->commit_txn(txn)))
        return txn_result(true, 0);
    }
    catch (abstract_db::abstract_abort_exception &ex)
    {
      db->abort_txn(txn);
    }
    return txn_result(false, 0);
  }

  static txn_result
  TxnWrite(bench_worker *w)
  {
    return static_cast<ycsb_worker *>(w)->txn_write();
  }

  txn_result
  txn_rmw()
  {
    void *const txn = db->new_txn(txn_flags, arena, txn_buf(), abstract_db::HINT_KV_RMW);
    scoped_str_arena s_arena(arena);
    try
    {
      const uint64_t key = r.next() % nkeys;
      ALWAYS_ASSERT(tbl->get(txn, u64_varkey(key).str(obj_key0), obj_v));
      computation_n += obj_v.size();
      tbl->put(txn, obj_key0, str().assign(YCSBRecordSize, 'c'));
      measure_txn_counters(txn, "txn_rmw");
      if (likely(db->commit_txn(txn)))
        return txn_result(true, 0);
    }
    catch (abstract_db::abstract_abort_exception &ex)
    {
      db->abort_txn(txn);
    }
    return txn_result(false, 0);
  }

  static txn_result
  TxnRmw(bench_worker *w)
  {
    return static_cast<ycsb_worker *>(w)->txn_rmw();
  }

  class worker_scan_callback : public abstract_ordered_index::scan_callback
  {
  public:
    worker_scan_callback() : n(0) {}
    virtual bool
    invoke(const char *, size_t, const string &value)
    {
      n += value.size();
      return true;
    }
    size_t n;
  };

  txn_result
  txn_scan()
  {
    void *const txn = db->new_txn(txn_flags, arena, txn_buf(), abstract_db::HINT_KV_SCAN);
    scoped_str_arena s_arena(arena);
    const size_t kstart = r.next() % nkeys;
    const string &kbegin = u64_varkey(kstart).str(obj_key0);
    const string &kend = u64_varkey(kstart + 100).str(obj_key1);
    worker_scan_callback c;
    try
    {
      tbl->scan(txn, kbegin, &kend, c);
      computation_n += c.n;
      measure_txn_counters(txn, "txn_scan");
      if (likely(db->commit_txn(txn)))
        return txn_result(true, 0);
    }
    catch (abstract_db::abstract_abort_exception &ex)
    {
      db->abort_txn(txn);
    }
    return txn_result(false, 0);
  }

  static txn_result
  TxnScan(bench_worker *w)
  {
    return static_cast<ycsb_worker *>(w)->txn_scan();
  }

  txn_result
  txn_swap()
  {
    // cout << "new tx" << endl;
    void *const txn = db->new_txn(txn_flags, arena, txn_buf(), abstract_db::HINT_DEFAULT);

    uint64_t k0, k1;

    // check if we have enough minted inputs
    bool mint_keys{false};
    if (m_current_input + 1 < ((worker_id + 1) * m_keys_per_worker))
    {
      k0 = m_current_input++;
      k1 = m_current_input++;
      mint_keys = true;
    }
    else
    {
      // use a created output
      ALWAYS_ASSERT(m_current_spent_output + 1 < m_current_output);
      k0 = m_current_spent_output++;
      k1 = m_current_spent_output++;
    }

    // generate two new outputs
    uint64_t k2, k3;
    k2 = m_current_output++;
    k3 = m_current_output++;

    auto inp_key0 = mint_keys ? get_mint_key(k0) : get_key(worker_id, k0);
    auto inp_key1 = mint_keys ? get_mint_key(k1) : get_key(worker_id, k1);
    auto out_key0 = get_key(worker_id, k2);
    auto out_key1 = get_key(worker_id, k3);

    try
    {
      // cout << "get keys" << endl;
      const auto v = std::string("1");
      //obj_v = "";
      // cout << "get key " << inp_key0 << " " << k0 << " " << mint_keys << " " << m_current_input << " " << m_keys_per_worker << " " << worker_id << endl;
      ALWAYS_ASSERT(tbl->get(txn, inp_key0, obj_v));
      // cout << "assert " << obj_v << " == " << v << endl;
      ALWAYS_ASSERT(obj_v == v);
      //obj_v = "";
      // cout << "get key " << inp_key1 << endl;
      ALWAYS_ASSERT(tbl->get(txn, inp_key1, obj_v));
      // cout << "assert " << obj_v << " == " << v << endl;
      ALWAYS_ASSERT(obj_v == v);

      // cout << "remove keys" << endl;

      tbl->remove(txn, inp_key0);
      tbl->remove(txn, inp_key1);

      // cout << "put keys" << endl;

      tbl->put(txn, out_key0, v);
      tbl->put(txn, out_key1, v);

      computation_n += obj_v.size();
      measure_txn_counters(txn, "txn_swap");
      // cout << "commit" << endl;
      if (likely(db->commit_txn(txn)))
      {
        // cout << "done" << endl;
        return txn_result(true, 0);
      }
    }
    catch (abstract_db::abstract_abort_exception &ex)
    {
      db->abort_txn(txn);
    }
    return txn_result(false, 0);
  }

  static txn_result
  TxnSwap(bench_worker *w)
  {
    return static_cast<ycsb_worker *>(w)->txn_swap();
  }

  virtual workload_desc_vec
  get_workload() const
  {
    // w.push_back(workload_desc("Read", 0.95, TxnRead));
    // w.push_back(workload_desc("ReadModifyWrite", 0.04, TxnRmw));
    // w.push_back(workload_desc("Write", 0.01, TxnWrite));

    // w.push_back(workload_desc("Read", 1.0, TxnRead));
    // w.push_back(workload_desc("Write", 1.0, TxnWrite));

    // YCSB workload "A" - 50/50 read/write
    // w.push_back(workload_desc("Read", 0.5, TxnRead));
    // w.push_back(workload_desc("Write", 0.5, TxnWrite));

    // YCSB workload custom - 80/20 read/write
    // w.push_back(workload_desc("Read",  0.8, TxnRead));
    // w.push_back(workload_desc("Write", 0.2, TxnWrite));

    workload_desc_vec w;
    /*unsigned m = 0;
    for (size_t i = 0; i < ARRAY_NELEMS(g_txn_workload_mix); i++)
      m += g_txn_workload_mix[i];
    ALWAYS_ASSERT(m == 100);
    if (g_txn_workload_mix[0])
      w.push_back(workload_desc("Read",  double(g_txn_workload_mix[0])/100.0, TxnRead));
    if (g_txn_workload_mix[1])
      w.push_back(workload_desc("Write",  double(g_txn_workload_mix[1])/100.0, TxnWrite));
    if (g_txn_workload_mix[2])
      w.push_back(workload_desc("ReadModifyWrite",  double(g_txn_workload_mix[2])/100.0, TxnRmw));
    if (g_txn_workload_mix[3])
      w.push_back(workload_desc("Scan",  double(g_txn_workload_mix[3])/100.0, TxnScan));*/
    w.push_back(workload_desc("Swap", 1.0, TxnSwap));
    return w;
  }

protected:
  virtual void
  on_run_setup() OVERRIDE
  {
    if (!pin_cpus)
      return;
    const size_t a = worker_id % coreid::num_cpus_online();
    const size_t b = a % nthreads;
    rcu::s_instance.pin_current_thread(b);
  }

  inline ALWAYS_INLINE string &
  str()
  {
    return *arena.next();
  }

private:
  abstract_ordered_index *tbl;

  string obj_key0;
  string obj_key1;
  string obj_v;

  uint64_t computation_n;

  uint64_t m_current_input;
  uint64_t m_keys_per_worker;
  uint64_t m_current_output{};
  uint64_t m_current_spent_output{};
};

static void
ycsb_load_keyrange(
    uint64_t keystart,
    uint64_t keyend,
    unsigned int pinid,
    abstract_db *db,
    abstract_ordered_index *tbl,
    str_arena &arena,
    uint64_t txn_flags,
    void *txn_buf)
{
  if (pin_cpus)
  {
    ALWAYS_ASSERT(pinid < nthreads);
    rcu::s_instance.pin_current_thread(pinid);
    rcu::s_instance.fault_region();
  }

  const size_t batchsize = (db->txn_max_batch_size() == -1) ? 10000 : db->txn_max_batch_size();
  ALWAYS_ASSERT(batchsize > 0);
  const size_t nkeys = keyend - keystart;
  ALWAYS_ASSERT(nkeys > 0);
  const size_t nbatches = nkeys < batchsize ? 1 : (nkeys / batchsize);
  for (size_t batchid = 0; batchid < nbatches;)
  {
    scoped_str_arena s_arena(arena);
    void *const txn = db->new_txn(txn_flags, arena, txn_buf);
    try
    {
      const size_t rend = (batchid + 1 == nbatches) ? keyend : keystart + ((batchid + 1) * batchsize);
      for (size_t i = batchid * batchsize + keystart; i < rend; i++)
      {
        ALWAYS_ASSERT(i >= keystart && i < keyend);
        const auto k = get_mint_key(i);
        const auto v = std::string("1");
        tbl->insert(txn, k, v);
      }
      if (db->commit_txn(txn))
        batchid++;
      else
        db->abort_txn(txn);
    }
    catch (abstract_db::abstract_abort_exception &ex)
    {
      db->abort_txn(txn);
    }
  }
  if (verbose)
    cerr << "[INFO] finished loading USERTABLE range [kstart="
         << keystart << ", kend=" << keyend << ") - nkeys: " << nkeys << endl;
}

class ycsb_usertable_loader : public bench_loader
{
public:
  ycsb_usertable_loader(unsigned long seed,
                        abstract_db *db,
                        const map<string, abstract_ordered_index *> &open_tables)
      : bench_loader(seed, db, open_tables)
  {
  }

protected:
  virtual void
  load()
  {
    abstract_ordered_index *tbl = open_tables.at("USERTABLE");
    const size_t nkeysperthd = nkeys / nthreads;
    for (size_t i = 0; i < nthreads; i++)
    {
      const size_t keystart = i * nkeysperthd;
      const size_t keyend = min((i + 1) * nkeysperthd, nkeys);
      ycsb_load_keyrange(
          keystart,
          keyend,
          i,
          db,
          tbl,
          arena,
          txn_flags,
          txn_buf());
    }
  }
};

class ycsb_parallel_usertable_loader : public bench_loader
{
public:
  ycsb_parallel_usertable_loader(unsigned long seed,
                                 abstract_db *db,
                                 const map<string, abstract_ordered_index *> &open_tables,
                                 unsigned int pinid,
                                 uint64_t keystart,
                                 uint64_t keyend)
      : bench_loader(seed, db, open_tables),
        pinid(pinid), keystart(keystart), keyend(keyend)
  {
    INVARIANT(keyend > keystart);
    if (verbose)
      cerr << "[INFO] YCSB par loader cpu " << pinid
           << " [" << keystart << ", " << keyend << ")" << endl;
  }

protected:
  virtual void
  load()
  {
    abstract_ordered_index *tbl = open_tables.at("USERTABLE");
    ycsb_load_keyrange(
        keystart,
        keyend,
        pinid,
        db,
        tbl,
        arena,
        txn_flags,
        txn_buf());
  }

private:
  unsigned int pinid;
  uint64_t keystart;
  uint64_t keyend;
};

class ycsb_bench_runner : public bench_runner
{
public:
  ycsb_bench_runner(abstract_db *db)
      : bench_runner(db)
  {
    open_tables["USERTABLE"] = db->open_index("USERTABLE", YCSBRecordSize);
  }

protected:
  virtual vector<bench_loader *>
  make_loaders()
  {
    vector<bench_loader *> ret;
    const unsigned long ncpus = coreid::num_cpus_online();
    if (enable_parallel_loading && nkeys >= nthreads)
    {
      // divide the key space amongst all the loaders
      const size_t nkeysperloader = nkeys / ncpus;
      if (nthreads > ncpus)
      {
        for (size_t i = 0; i < ncpus; i++)
        {
          const uint64_t kend = (i + 1 == ncpus) ? nkeys : (i + 1) * nkeysperloader;
          ret.push_back(
              new ycsb_parallel_usertable_loader(
                  0, db, open_tables, i,
                  i * nkeysperloader, kend));
        }
      }
      else
      {
        // load balance the loaders amongst numa nodes in RR fashion
        //
        // XXX: here we hardcode an assumption about the NUMA topology of
        // the system
        const vector<unsigned> numa_nodes_used = get_numa_nodes_used(nthreads);

        // assign loaders to cores based on numa node assignment in RR fashion
        const unsigned loaders_per_node = ncpus / numa_nodes_used.size();

        vector<unsigned> node_allocations(numa_nodes_used.size(), loaders_per_node);
        // RR the remaining
        for (unsigned i = 0;
             i < (ncpus - loaders_per_node * numa_nodes_used.size());
             i++)
          node_allocations[i]++;

        size_t loader_i = 0;
        for (size_t i = 0; i < numa_nodes_used.size(); i++)
        {
          // allocate loaders_per_node loaders to this numa node
          const vector<unsigned> cpus = numa_node_to_cpus(numa_nodes_used[i]);
          const vector<unsigned> cpus_avail = exclude(cpus, nthreads);
          const unsigned nloaders = node_allocations[i];
          for (size_t j = 0; j < nloaders; j++, loader_i++)
          {
            const uint64_t kend = (loader_i + 1 == ncpus) ? nkeys : (loader_i + 1) * nkeysperloader;
            ret.push_back(
                new ycsb_parallel_usertable_loader(
                    0, db, open_tables, cpus_avail[j % cpus_avail.size()],
                    loader_i * nkeysperloader, kend));
          }
        }
      }
    }
    else
    {
      ret.push_back(new ycsb_usertable_loader(0, db, open_tables));
    }
    return ret;
  }

  virtual vector<bench_worker *>
  make_workers()
  {
    const unsigned alignment = coreid::num_cpus_online();
    const int blockstart =
        coreid::allocate_contiguous_aligned_block(nthreads, alignment);
    ALWAYS_ASSERT(blockstart >= 0);
    ALWAYS_ASSERT((blockstart % alignment) == 0);
    fast_random r(8544290);
    vector<bench_worker *> ret;
    for (size_t i = 0; i < nthreads; i++)
      ret.push_back(
          new ycsb_worker(
              i, r.next(), db, open_tables,
              &barrier_a, &barrier_b));
    return ret;
  }

private:
  static vector<unsigned>
  get_numa_nodes_used(unsigned nthds)
  {
    // assuming CPUs [0, nthds) are used, what are all the
    // NUMA nodes touched by [0, nthds)
    set<unsigned> ret;
    for (unsigned i = 0; i < nthds; i++)
    {
      const int node = numa_node_of_cpu(i);
      ALWAYS_ASSERT(node >= 0);
      ret.insert(node);
    }
    return vector<unsigned>(ret.begin(), ret.end());
  }

  static vector<unsigned>
  numa_node_to_cpus(unsigned node)
  {
    struct bitmask *bm = numa_allocate_cpumask();
    ALWAYS_ASSERT(!::numa_node_to_cpus(node, bm));
    vector<unsigned> ret;
    for (int i = 0; i < numa_num_configured_cpus(); i++)
      if (numa_bitmask_isbitset(bm, i))
        ret.push_back(i);
    numa_free_cpumask(bm);
    return ret;
  }

  static vector<unsigned>
  exclude(const vector<unsigned> &cpus, unsigned nthds)
  {
    vector<unsigned> ret;
    for (auto n : cpus)
      if (n < nthds)
        ret.push_back(n);
    return ret;
  }
};

bench_runner *
ycsb_do_test(abstract_db *db, int argc, char **argv)
{
  // nkeys = size_t(scale_factor * 1000.0);
  nkeys = 100'000'000;
  ALWAYS_ASSERT(nkeys > 0);

  // parse options
  optind = 1;
  while (1)
  {
    static struct option long_options[] = {
        {"workload-mix", required_argument, 0, 'w'},
        {0, 0, 0, 0}};
    int option_index = 0;
    int c = getopt_long(argc, argv, "w:", long_options, &option_index);
    if (c == -1)
      break;
    switch (c)
    {
    case 0:
      if (long_options[option_index].flag != 0)
        break;
      abort();
      break;

    case 'w':
    {
      const vector<string> toks = split(optarg, ',');
      ALWAYS_ASSERT(toks.size() == ARRAY_NELEMS(g_txn_workload_mix));
      unsigned s = 0;
      for (size_t i = 0; i < toks.size(); i++)
      {
        unsigned p = strtoul(toks[i].c_str(), nullptr, 10);
        ALWAYS_ASSERT(p >= 0 && p <= 100);
        s += p;
        g_txn_workload_mix[i] = p;
      }
      ALWAYS_ASSERT(s == 100);
    }
    break;

    case '?':
      /* getopt_long already printed an error message. */
      exit(1);

    default:
      abort();
    }
  }

  if (verbose)
  {
    cerr << "ycsb settings:" << endl;
    cerr << "  workload_mix: "
         << format_list(g_txn_workload_mix, g_txn_workload_mix + ARRAY_NELEMS(g_txn_workload_mix))
         << endl;
  }

  auto r = new ycsb_bench_runner(db);
  r->run_without_stats();
  return r;
}
