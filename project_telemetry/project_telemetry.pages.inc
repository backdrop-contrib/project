<?php
/**
 * @file
 * Menu callbacks that provide front-end pages for the Project Telemetry module.
 */

/**
 * Menu callback; Display Telemetry data for a node.
 *
 * @param Node $node
 *   Either a Project or Project Release node.
 */
function project_telemetry_page(Node $node) {
  if ($node->type === 'project') {
    $output = project_telemetry_project_page($node);
  }
  else {
    $output = project_telemetry_release_page($node);
  }
  return $output;
}

/**
 * Menu callback; Display Telemetry data for a project node.
 *
 * @param Node $node
 *   The project node object for which we're displaying Telemetry data.
 */
function project_telemetry_project_page(Node $node) {
  $telemetry_aggregates = project_telemetry_load_aggregates($node->project['name']);

  $page = array();

  $help = '';
  $help .= '<p>' . t('Below is Telemetry data collected for this project.') . '</p>';

  $page['help'] = array(
    '#type' => 'help',
    '#markup' => $help
  );

  $header = array(
    t('Value'),
    t('Count'),
    t('Percentage'),
  );

  foreach ($telemetry_aggregates as $item_key => $aggregate_values) {
    $rows = array();
    foreach ($aggregate_values as $aggregate_value) {
      $row = array();
      $row[] = check_plain($aggregate_value['item_value']);
      $row[] = $aggregate_value['item_count'];
      $row[] = $aggregate_value['percentage'] * 100 . '%';;
      $rows[] = $row;
    }
    $page['items'][$item_key] = array(
      '#theme' => 'table',
      '#header' => $header,
      '#caption' => $item_key,
      '#rows' => $rows,
      '#sticky' => FALSE,
    );
  }

  return $page;

}

/**
 * Menu callback; Display Telemetry data for a project release node.
 *
 * @param Node $node
 *   The project release node object for which we're displaying Telemetry data.
 */
function project_telemetry_release_page(Node $node) {
  // TODO: Per-release Telemetry data?
  $page = 'Per release Telemetry not yet implemented';
}
