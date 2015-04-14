#!/usr/bin/php
<?php

/**
 * @file
 * Processes the project_usage statistics.
 *
 * @author Andrew Morton (http://drupal.org/user/34869)
 * @author Derek Wright (http://drupal.org/user/46549)
 */

// Define the root directory of the Backdrop installation.
if (getopt('root')) {
  define('BACKDROP_ROOT', getopt('root'));
}
else {
  $cwd = getcwd();
  if (($pos = strpos($cwd, '/sites/')) || ($pos = strpos($cwd, '/modules/'))) {
    $cwd = substr($cwd, 0, $pos);
  }
  define('BACKDROP_ROOT', $cwd);
}

// The hostname of your site. Required so that when we bootstrap Backdrop in
// this script, we find the right settings.php file in your sites folder.
$url = getopt('url');
define('HTTP_HOST', $url ? parse_url($url, PHP_URL_HOST) : '');

// ------------------------------------------------------------
// Initialization
// (Real work begins here, nothing else to customize)
// ------------------------------------------------------------

// Check if all required variables are defined
$vars = array(
  'BACKDROP_ROOT' => BACKDROP_ROOT,
  'HTTP_HOST' => HTTP_HOST,
);
$fatal_err = FALSE;
foreach ($vars as $name => $val) {
  if (empty($val)) {
    print "ERROR: \"$name\" constant not defined, aborting\n";
    $fatal_err = TRUE;
  }
}
if ($fatal_err) {
  exit(1);
}

$script_name = $argv[0];

// Setup variables for Backdrop bootstrap
$_SERVER['HTTP_HOST'] = HTTP_HOST;
$_SERVER['REMOTE_ADDR'] = '127.0.0.1';
$_SERVER['REQUEST_URI'] = '/' . $script_name;
$_SERVER['SCRIPT_NAME'] = '/' . $script_name;
$_SERVER['PHP_SELF'] = '/' . $script_name;
$_SERVER['SCRIPT_FILENAME'] = $_SERVER['PWD'] .'/'. $script_name;
$_SERVER['PATH_TRANSLATED'] = $_SERVER['SCRIPT_FILENAME'];

if (!chdir(BACKDROP_ROOT)) {
  print "ERROR: Can't chdir(BACKDROP_ROOT), aborting.\n";
  exit(1);
}

require_once './core/includes/bootstrap.inc';
backdrop_bootstrap(BACKDROP_BOOTSTRAP_FULL);

if (!module_exists('project_usage')) {
  print "ERROR: Project usage module does not exist, aborting.\n";
  exit(1);
}

// Load the API functions we need for manipulating dates and timestamps.
module_load_include('inc', 'project_usage', 'project_usage.date');

// ------------------------------------------------------------
// Call the daily and weekly processing tasks as needed.
// ------------------------------------------------------------

$now = time();

// Figure out if it's been 24 hours since our last daily processing.
if (state_get('project_usage_last_daily', 0) <= ($now - PROJECT_USAGE_DAY)) {
  project_usage_process_daily();
  state_set('project_usage_last_daily', $now);
}

// We can't process the weekly data until the week has completed. To see if
// there's data available: determine the last time we completed the weekly
// processing and compare that to the start of this week. If the last
// weekly processing occurred before the current week began then there should
// be one (or more) week's worth of data ready to process.
$default = $now - state_get('project_usage_life_daily', 4 * PROJECT_USAGE_WEEK);
$last_weekly = state_get('project_usage_last_weekly', $default);
$current_week_start = project_usage_weekly_timestamp(NULL, 0);
if ($last_weekly <= $current_week_start) {
  project_usage_process_weekly($last_weekly);
  state_set('project_usage_last_weekly', $now);
  // Reset the list of active weeks.
  project_usage_get_active_weeks(TRUE);
}

// Wipe the cache of all expired usage pages.
cache('project_usage')->flush();

/**
 * Process all the raw data up to the previous day.
 *
 * The primary key on the {project_usage_raw} table will prevent duplicate
 * records provided we process them once the day is complete. If we pull them
 * out too soon and the site checks in again they will be counted twice.
 */
function project_usage_process_daily() {
  // Timestamp for beginning of the previous day.
  $timestamp = project_usage_daily_timestamp(NULL, 1);
  $time_0 = microtime(TRUE);

  watchdog('project_usage', 'Starting to process daily usage data for !date.', array('!date' => format_date($timestamp, 'custom', 'Y-m-d')));

  // Move usage records with project node IDs into the daily table and remove
  // the rest.
  db_query("INSERT INTO {project_usage_day} (timestamp, site_key, pid, nid, tid, ip_addr) SELECT timestamp, site_key, pid, nid, tid, ip_addr FROM {project_usage_raw} WHERE timestamp < %d AND pid <> 0", $timestamp);
  $num_new_day_rows = db_affected_rows();
  db_query("DELETE FROM {project_usage_raw} WHERE timestamp < %d", $timestamp);
  $num_deleted_raw_rows = db_affected_rows();
  $time_1 = microtime(TRUE);
  $substitutions = array(
    '!day_rows' => format_plural($num_new_day_rows, '1 row', '@count rows'),
    '!raw_rows' => format_plural($num_deleted_raw_rows, '1 row', '@count rows'),
    '!delta' => format_interval($time_1 - $time_0),
  );
  watchdog('project_usage', 'Moved usage from raw to daily: !day_rows added to {project_usage_day}, !raw_rows deleted from {project_usage_raw} (!delta seconds).', $substitutions);

  // Remove old daily records.
  $seconds = state_get('project_usage_life_daily', 4 * PROJECT_USAGE_WEEK);
  $result = db_query("DELETE FROM {project_usage_day} WHERE timestamp < %d", time() - $seconds);
  $time_2 = microtime(TRUE);
  $substitutions = array(
    '!rows' => format_plural($result->rowCount(), '1 old daily row', '@count old daily rows'),
    '!delta' => format_interval($time_2 - $time_1),
  );
  watchdog('project_usage', 'Removed !rows (!delta seconds).', $substitutions);

  watchdog('project_usage', 'Completed daily usage data processing (total time: !delta seconds).', array('!delta' => format_interval($time_2 - $time_0)));
}

/**
 * Compute the weekly summaries for the week starting at the given timestamp.
 *
 * @param $timestamp
 *   UNIX timestamp indicating the last time weekly stats were processed.
 */
function project_usage_process_weekly($timestamp) {
  watchdog('project_usage', 'Starting to process weekly usage data.');
  $time_0 = microtime(TRUE);

  // Get all the weeks since we last ran.
  $weeks = project_usage_get_weeks_since($timestamp);
  // Skip the last entry since it's the current, incomplete week.
  $count = count($weeks) - 1;
  for ($i = 0; $i < $count; $i++) {
    $start = $weeks[$i];
    $end = $weeks[$i + 1];
    $date = format_date($start, 'custom', 'Y-m-d');
    $time_1 = microtime(TRUE);

    // Try to compute the usage tallies per project and per release. If there
    // is a problem--perhaps some rows existed from a previous, incomplete
    // run that are preventing inserts, throw a watchdog error.

    $sql = "INSERT INTO {project_usage_week_project} (nid, timestamp, tid, count) SELECT pid, %d, tid, COUNT(DISTINCT site_key) FROM {project_usage_day} WHERE timestamp >= %d AND timestamp < %d AND pid <> 0 GROUP BY pid, tid";
    $query_args = array($start, $start, $end);
    $result = db_query($sql, $query_args);
    $time_2 = microtime(TRUE);
    _db_query_callback($query_args, TRUE);
    $substitutions = array(
      '!date' => $date,
      '%query' => preg_replace_callback(DB_QUERY_REGEXP, '_db_query_callback', $sql),
      '!projects' => format_plural(db_affected_rows(), '1 project', '@count projects'),
      '!delta' => format_interval($time_2 - $time_1),
    );
    if (!$result) {
      watchdog('project_usage', 'Query failed inserting weekly project tallies for !date, query: %query (!delta seconds).', $substitutions, WATCHDOG_ERROR);
    }
    else {
      watchdog('project_usage', 'Computed weekly project tallies for !date for !projects (!delta seconds).', $substitutions);
    }

    $sql = "INSERT INTO {project_usage_week_release} (nid, timestamp, count) SELECT nid, %d, COUNT(DISTINCT site_key) FROM {project_usage_day} WHERE timestamp >= %d AND timestamp < %d AND nid <> 0 GROUP BY nid";
    $query_args = array($start, $start, $end);
    $result = db_query($sql, $query_args);
    $time_3 = microtime(TRUE);
    _db_query_callback($query_args, TRUE);
    $substitutions = array(
      '!date' => $date,
      '!releases' => format_plural(db_affected_rows(), '1 release', '@count releases'),
      '%query' => preg_replace_callback(DB_QUERY_REGEXP, '_db_query_callback', $sql),
      '!delta' => format_interval($time_3 - $time_2),
    );
    if (!$result) {
      watchdog('project_usage', 'Query failed inserting weekly release tallies for !date, query: %query (!delta seconds).', $substitutions, WATCHDOG_ERROR);
    }
    else {
      watchdog('project_usage', 'Computed weekly release tallies for !date for !releases (!delta seconds).', $substitutions);
    }
  }

  // Remove any tallies that have aged out.
  $time_4 = microtime(TRUE);
  $project_life = config_get('project_usage.settings', 'life_weekly_project');
  db_query("DELETE FROM {project_usage_week_project} WHERE timestamp < %d", REQUEST_TIME - $project_life);
  $time_5 = microtime(TRUE);
  $substitutions = array(
    '!rows' => format_plural(db_affected_rows(), '1 old weekly project row', '@count old weekly project rows'),
    '!delta' => format_interval($time_5 - $time_4),
  );
  watchdog('project_usage', 'Removed !rows (!delta seconds).', $substitutions);

  $release_life = config_get('project_usage.settings', 'life_weekly_release');
  db_query("DELETE FROM {project_usage_week_release} WHERE timestamp < %d", REQUEST_TIME - $release_life);
  $time_6 = microtime(TRUE);
  $substitutions = array(
    '!rows' => format_plural(db_affected_rows(), '1 old weekly release row', '@count old weekly release rows'),
    '!delta' => format_interval($time_6 - $time_5),
  );
  watchdog('project_usage', 'Removed !rows (!delta seconds).', $substitutions);

  watchdog('project_usage', 'Completed weekly usage data processing (total time: !delta seconds).', array('!delta' => format_interval($time_6 - $time_0)));
}
