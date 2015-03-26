<?php
/**
 * @file
 * Code for the "releases" subtab to the edit tab on project nodes.
 */

/**
 * Releases subtab for the project edit tab.
 */
function project_release_project_edit_releases($node) {
  project_project_set_breadcrumb($node);
  backdrop_set_title(check_plain($node->title));
  return backdrop_get_form('project_release_project_edit_form', $node);
}

function project_release_project_edit_form($form, $form_state, $node) {
  $api_versions = config_get('project_release.settings', 'api_versions');

  // Since this form is used relatively infrequently, don't allow the js to be aggregated.
  backdrop_add_js(backdrop_get_path('module', 'project_release') . '/project_release.js', array('preprocess' => FALSE));

  // Get all the data about major versions for this project.
  $query = db_select('project_release_supported_versions', 'psrv');
  $query->condition('psrv.nid', $node->nid);
  $query->condition('psrv.version_api', $api_versions);
  $query->fields('psrv', array('version_api', 'version_major', 'snapshot', 'supported', 'recommended'));
  $query->orderBy('psrv.version_api');
  $query->orderBy('psrv.version_major');
  $data = array();
  foreach ($query->execute() as $object) {
    if (empty($data[$object->version_api])) {
      $data[$object->version_api] = array(
        'name' => $object->name,
        'majors' => array(),
      );
    }
    $data[$object->version_api]['majors'][$object->version_major] = array(
      '#snapshot' => $object->snapshot ? TRUE : FALSE,
      '#supported' => $object->supported ? TRUE : FALSE,
      '#recommended' => $object->recommended ? TRUE : FALSE,
    );
  }
  if (empty($data)) {
    $form['help'] = array(
      '#markup' => '<p>' . t('No release versions for this project exist.') . '</p>',
    );
  }
  else {
    if (!empty($api_versions)) {
      $form['help'] = array(
        '#markup' => '<p>' . t('This page allows you to define which major versions are supported. You can also control if the development snapshot releases should be displayed on the front page of the project. Finally, you can select which major version (if any) should be recommended for new users to download.') . '</p>',
      );
    }
    // Build the form elements for supported and recommended major versions.
    $form['api'] = array(
      '#tree' => TRUE,
      '#theme' => 'project_release_edit_table',
    );
    foreach ($data as $api_version => $api_data) {
      $form['api'][$api_version] = array(
        'major' => array(),
      );
      $recommended_version = -1;
      $recommended_options = array(-1 => t('None'));
      foreach ($api_data['majors'] as $major_version => $major_data) {
        if ($major_data['#recommended'] == TRUE) {
          $recommended_version = $major_version;
        }

        $recommended_options[$major_version] = $major_version;
        $current_recommended = project_release_get_current_recommended($node->nid, $api_version, $major_version);
        $form['api'][$api_version]['major'][$major_version] = array(
          'current' => array(
            '#markup' => (!empty($current_recommended)) ? check_plain($current_recommended->project_release['version']) : '',
          ),
          'supported' => array(
            '#type' => 'checkbox',
            '#title' => t('Supported'),
            '#default_value' => $major_data['#supported'],
            '#attributes' => array('class' => array('supported')),
          ),
          'snapshot' => array(
            '#type' => 'checkbox',
            '#title' => t('Show snapshot release'),
            '#default_value' => $major_data['#snapshot'],
            '#attributes' => array('class' => array('snapshot')),
          ),
        );
      }
      $form['api'][$api_version]['recommended'] = array(
        '#title' => t('Recommended major version'),
        '#type' => 'select',
        '#options' => $recommended_options,
        '#default_value' => $recommended_version,
        '#prefix' => '<div class="container-inline">',
        '#suffix' => '</div>',
        '#attributes' => array('class' => array('recommended')),
      );
    }

    $form['actions']['#type'] = 'actions';
    $form['actions']['submit'] = array(
      '#type' => 'submit',
      '#value' => t('Save'),
    );
  }

  $types = project_release_release_node_types();
  // todo this uses the first release node type. What to do if there are
  // multiple?
  $links['links'] = array(
    'add_new_release' => array(
      'title' => t('Add new release'), 'href' => 'node/add/' . str_replace('_', '-', reset($types)) . '/' . $node->nid,
    ),
  );

  $form['create_link'] = array(
    '#markup' => '<p>' . theme('links', $links) . '</p>',
    '#weight' => 50,
  );

  return $form;
}

function theme_project_release_edit_table($v) {
  $output = '';

  $header = array(
    t('Major version'),
    t('Current Release'),
    t('Supported'),
    array(
      'data' => t('Show snapshot release'),
      'colspan' => 2,
    ),
  );
  foreach (element_children($v['api']) as $tid) {
    $output .= '<h3>'. $v['api'][$tid]['#api_term_name'] .'</h3>';
    $rows = array();
    krsort($v['api'][$tid]['major']);
    foreach (element_children($v['api'][$tid]['major']) as $major) {
      $row = array();
      $row[] = $major;
      $row[] = backdrop_render($v['api'][$tid]['major'][$major]['current']);
      // We want to unset the titles for each element, since we already have
      // table headers to label each column.
      unset($v['api'][$tid]['major'][$major]['supported']['#title']);
      $row[] = backdrop_render($v['api'][$tid]['major'][$major]['supported']);
      unset($v['api'][$tid]['major'][$major]['snapshot']['#title']);
      $row[] = backdrop_render($v['api'][$tid]['major'][$major]['snapshot']);
      $rows[] = $row;
    }
    // Finally, add a row for the currently recommended version.
    $row = array();
    $row[] = array(
      'data' => backdrop_render($v['api'][$tid]['recommended']),
      'colspan' => 5,
    );
    $rows[] = $row;
    $output .= theme('table', array('header' => $header, 'rows' => $rows));
  }

  return $output;
}

/**
 * Validates the project form regarding release-specific fields.
 *
 * @ingroup project_release_fapi
 * @see project_release_project_edit_releases()
 */
function project_release_project_edit_form_validate($form, &$form_state) {
  if (isset($form_state['values']['api'])) {
    foreach ($form_state['values']['api'] as $tid => $api_info) {
      $supported = FALSE;
      // First, we just iterate through to see if *any* majors are supported.
      foreach ($api_info['major'] as $major => $flags) {
        // At least 1 major is supported, so validate the settings.
        if ($flags['supported'] == FALSE && $flags['snapshot'] == TRUE) {
          form_error($form['api'][$tid]['major'][$major]['snapshot'], t('You can not show a snapshot release for a major version that is not supported for %api_term_name.', array('%api_term_name' => $form['api'][$tid]['#api_term_name'])));
        }
        if ($flags['supported'] == FALSE && $api_info['recommended'] == $major) {
          form_error($form['api'][$tid]['recommended'], t('You can not recommend a major version that is not supported for %api_term_name.', array('%api_term_name' => $form['api'][$tid]['#api_term_name'])));
        }
      }
    }
  }
}

/**
 * Submit handler when project admins use the releases subtab.
 *
 * @ingroup project_release_fapi
 * @see project_release_project_edit_releases()
 */
function project_release_project_edit_form_submit($form, &$form_state) {
  $nid = $form_state['build_info']['args'][0]->nid;

  if (!empty($form_state['values']['api'])) {
    $updates = array();
    foreach ($form_state['values']['api'] as $tid => $values) {
      if (!empty($values['major'])) {
        foreach ($values['major'] as $major => $major_values) {
          $major_values['recommended'] = ($values['recommended'] == $major) ? 1 : 0;
          $existing = db_select('project_release_supported_versions', 'psrv')
            ->fields('psrv', array('supported', 'recommended', 'snapshot'))
            ->condition('nid', $nid)
            ->condition('tid', $tid)
            ->condition('major', $major)
            ->execute()->fetchAssoc();
          if ($existing !== FALSE) {
            if (count(array_diff_assoc($major_values, $existing)) > 0) {
              db_update('project_release_supported_versions')
                ->fields($major_values)
                ->condition('nid', $nid)
                ->condition('tid', $tid)
                ->condition('major', $major)
                ->execute();
              $updates[] = array('api_tid' => $tid, 'major' => $major);
            }
          }
          else {
            db_insert('project_release_supported_versions')
              ->fields(array(
                'nid' => $nid,
                'tid' => $tid,
                'major' => $major,
                'supported' => $major_values['supported'],
                'recommended' => $major_values['recommended'],
                'snapshot' => $major_values['snapshot'],
              ))
              ->execute();
            $updates[] = array('api_tid' => $tid, 'major' => $major);
          }
        }
      }
    }
  }
  db_update('node')
    ->fields(array('changed' => time()))
    ->condition('nid', $nid)
    ->execute();

  backdrop_set_message(t('Release settings have been saved.'));

  // Since we've possibly adjusted the status/recommended releases, we need to
  // recompute the update status for each set of releases and re-generate the
  // download table.
  if (!empty($updates)) {
    backdrop_register_shutdown_function('project_release_compute_update_status_multiple', $nid, $updates);
  }

}
