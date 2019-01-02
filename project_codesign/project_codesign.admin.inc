<?php

/**
 * @file
 */

function project_codesign_admin_form($form, &$form_state) {
  $signing_profiles = codesign_get_signing_profiles();

  $headers = array(
    'name' => t('Name'),
    'crypto_engine' => t('Engine'),
    'profile_id' => t('Profile ID'),
  );

  foreach($signing_profiles as $key => $profile) {
    $signing_profiles[$key]['name'] = check_plain($profile['name']);
  }

  $form['#config'] = 'project_codesign.settings';
  $form['title'] = array(
    '#type' => 'item',
    '#title' => t('Enabled Signing Profiles'),
    '#description' => t('Check the profiles that should be used to sign modules.'),
  );
  $form['enabled_profiles'] = array(
    '#type' => 'tableselect',
    '#header' => $headers,
    '#options' => $signing_profiles,
    '#empty' => t('No signing keys found.'),
    '#default_value' => config('project_codesign.settings')->get('enabled_profiles'),
  );

  return system_settings_form($form);
}
