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

  $form['#config'] = 'project_codesign.settings';
  $form['enabled_profiles'] = array(
    '#type' => 'tableselect',
    '#header' => $headers,
    '#options' => $signing_profiles,
    '#empty' => t('No signing keys found.'),
    '#default_value' => config('project_codesign.settings')->get('enabled_profiles'),
  );

  return system_settings_form($form);
}
