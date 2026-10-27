# Waits for DPT markers written in the background by the drop index thread,
# then prints and moves the baseline as dpt_markers_written.inc does.
--let $wait_condition = SELECT variable_value > $dpt_markers FROM performance_schema.global_status WHERE variable_name = 'rocksdb_delete_dpt_markers'
--source include/wait_condition.inc
--source suite/rocksdb/include/dpt_markers_written.inc
