# Prints whether DPT markers were written since the last call, then moves the
# baseline. Set $dpt_markers to the current counter value before the first
# call.
--let $dpt_markers_now = query_get_value(SHOW GLOBAL STATUS LIKE 'rocksdb_delete_dpt_markers', Value, 1)
--disable_query_log
--eval SELECT $dpt_markers_now > $dpt_markers AS markers_written
--enable_query_log
--let $dpt_markers = $dpt_markers_now
