// This autogenerated skeleton file illustrates how to build a server.
// You should copy it to another filename to avoid overwriting it.

#include "Cassandra.h"
#include <protocol/TBinaryProtocol.h>
#include <server/TSimpleServer.h>
#include <transport/TServerSocket.h>
#include <transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using boost::shared_ptr;

using namespace  ::org::apache::cassandra;

class CassandraHandler : virtual public CassandraIf {
 public:
  CassandraHandler() {
    // Your initialization goes here
  }

  void login(const AuthenticationRequest& auth_request) {
    // Your implementation goes here
    printf("login\n");
  }

  void set_keyspace(const std::string& keyspace) {
    // Your implementation goes here
    printf("set_keyspace\n");
  }

  void get(ColumnOrSuperColumn& _return, const std::string& key, const ColumnPath& column_path, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("get\n");
  }

  void get_slice(std::vector<ColumnOrSuperColumn> & _return, const std::string& key, const ColumnParent& column_parent, const SlicePredicate& predicate, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("get_slice\n");
  }

  int32_t get_count(const std::string& key, const ColumnParent& column_parent, const SlicePredicate& predicate, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("get_count\n");
  }

  void multiget_slice(std::map<std::string, std::vector<ColumnOrSuperColumn> > & _return, const std::vector<std::string> & keys, const ColumnParent& column_parent, const SlicePredicate& predicate, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("multiget_slice\n");
  }

  void multiget_count(std::map<std::string, int32_t> & _return, const std::vector<std::string> & keys, const ColumnParent& column_parent, const SlicePredicate& predicate, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("multiget_count\n");
  }

  void get_range_slices(std::vector<KeySlice> & _return, const ColumnParent& column_parent, const SlicePredicate& predicate, const KeyRange& range, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("get_range_slices\n");
  }

  void get_paged_slice(std::vector<KeySlice> & _return, const std::string& column_family, const KeyRange& range, const std::string& start_column, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("get_paged_slice\n");
  }

  void get_indexed_slices(std::vector<KeySlice> & _return, const ColumnParent& column_parent, const IndexClause& index_clause, const SlicePredicate& column_predicate, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("get_indexed_slices\n");
  }

  void insert(const std::string& key, const ColumnParent& column_parent, const Column& column, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("insert\n");
  }

  void add(const std::string& key, const ColumnParent& column_parent, const CounterColumn& column, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("add\n");
  }

  void remove(const std::string& key, const ColumnPath& column_path, const int64_t timestamp, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("remove\n");
  }

  void remove_counter(const std::string& key, const ColumnPath& path, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("remove_counter\n");
  }

  void batch_mutate(const std::map<std::string, std::map<std::string, std::vector<Mutation> > > & mutation_map, const ConsistencyLevel::type consistency_level) {
    // Your implementation goes here
    printf("batch_mutate\n");
  }

  void truncate(const std::string& cfname) {
    // Your implementation goes here
    printf("truncate\n");
  }

  void describe_schema_versions(std::map<std::string, std::vector<std::string> > & _return) {
    // Your implementation goes here
    printf("describe_schema_versions\n");
  }

  void describe_keyspaces(std::vector<KsDef> & _return) {
    // Your implementation goes here
    printf("describe_keyspaces\n");
  }

  void describe_cluster_name(std::string& _return) {
    // Your implementation goes here
    printf("describe_cluster_name\n");
  }

  void describe_version(std::string& _return) {
    // Your implementation goes here
    printf("describe_version\n");
  }

  void describe_ring(std::vector<TokenRange> & _return, const std::string& keyspace) {
    // Your implementation goes here
    printf("describe_ring\n");
  }

  void describe_token_map(std::map<std::string, std::string> & _return) {
    // Your implementation goes here
    printf("describe_token_map\n");
  }

  void describe_partitioner(std::string& _return) {
    // Your implementation goes here
    printf("describe_partitioner\n");
  }

  void describe_snitch(std::string& _return) {
    // Your implementation goes here
    printf("describe_snitch\n");
  }

  void describe_keyspace(KsDef& _return, const std::string& keyspace) {
    // Your implementation goes here
    printf("describe_keyspace\n");
  }

  void describe_splits(std::vector<std::string> & _return, const std::string& cfName, const std::string& start_token, const std::string& end_token, const int32_t keys_per_split) {
    // Your implementation goes here
    printf("describe_splits\n");
  }

  void system_add_column_family(std::string& _return, const CfDef& cf_def) {
    // Your implementation goes here
    printf("system_add_column_family\n");
  }

  void system_drop_column_family(std::string& _return, const std::string& column_family) {
    // Your implementation goes here
    printf("system_drop_column_family\n");
  }

  void system_add_keyspace(std::string& _return, const KsDef& ks_def) {
    // Your implementation goes here
    printf("system_add_keyspace\n");
  }

  void system_drop_keyspace(std::string& _return, const std::string& keyspace) {
    // Your implementation goes here
    printf("system_drop_keyspace\n");
  }

  void system_update_keyspace(std::string& _return, const KsDef& ks_def) {
    // Your implementation goes here
    printf("system_update_keyspace\n");
  }

  void system_update_column_family(std::string& _return, const CfDef& cf_def) {
    // Your implementation goes here
    printf("system_update_column_family\n");
  }

  void execute_cql_query(CqlResult& _return, const std::string& query, const Compression::type compression) {
    // Your implementation goes here
    printf("execute_cql_query\n");
  }

  void prepare_cql_query(CqlPreparedResult& _return, const std::string& query, const Compression::type compression) {
    // Your implementation goes here
    printf("prepare_cql_query\n");
  }

  void execute_prepared_cql_query(CqlResult& _return, const int32_t itemId, const std::vector<std::string> & values) {
    // Your implementation goes here
    printf("execute_prepared_cql_query\n");
  }

  void set_cql_version(const std::string& version) {
    // Your implementation goes here
    printf("set_cql_version\n");
  }

};

int main(int argc, char **argv) {
  int port = 9090;
  shared_ptr<CassandraHandler> handler(new CassandraHandler());
  shared_ptr<TProcessor> processor(new CassandraProcessor(handler));
  shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
  shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
  shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

  TSimpleServer server(processor, serverTransport, transportFactory, protocolFactory);
  server.serve();
  return 0;
}

