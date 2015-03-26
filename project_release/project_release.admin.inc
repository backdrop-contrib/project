<?php
/**
 * @file
 * Administrative menu callbacks for the Project Release module.
 */

/**
 * Main settings form for project release module.
 */
function project_release_settings_form($form, $form_state) {
  $config = config('project_release.settings');

  $form['api_versions'] = array(
    '#type' => 'textarea',
    '#title' => t('Major API versions'),
    '#default_value' => implode("\n", $config->get('api_versions')),
    '#description' => t('Enter one major API version per line. An API version is usually formatted as 1.x, 2.x, etc.'),
  );

  $form['actions']['#type'] = 'actions';
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Submit'),
  );

  return $form;
}

function project_release_settings_form_submit($form, &$form_state) {
  $api_versions = explode("\n", $form_state['values']['api_versions']);
  foreach ($api_versions as $key => $value) {
    $api_versions[$key] = trim($value);
  }
  $config = config('project_release.settings');
  $config->set('api_versions', $api_versions);
  $config->save();

  backdrop_set_message(t('The configuration options have been saved.'));
}