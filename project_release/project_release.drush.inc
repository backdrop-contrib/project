<?php

/**
 * @file
 * Drush command for packaging files for release nodes.
 *
 * @author Derek Wright (http://drupal.org/user/46549)
 */

function project_release_drush_command() {
  return array(
    'release-package-run' => array(
      'description' => 'Run queued release packaging. Creates packages AND writes release history XML files.',
    ),
    'release-package' => array(
      'description' => 'Package files for release nodes.',
      'arguments' => array(
        'type' => '"branch" or "tag", required.',
        'project_id' => 'Project, either a Node ID or machine name. Omit to build all projects.'
      ),
      'required-arguments' => TRUE,
    ),
    'release-create-history' => array(
      'description' => 'Creates XML release history for projects.',
      'arguments' => array(
        'project_id' => 'Project, either a Node ID or machine name. Omit to build all projects.',
      ),
    ),
    'release-check-status' => array(
      'description' => 'Check the status and visibility for releases.',
      'arguments' => array(
        'project_id' => 'Project, either a Node ID or machine name. Omit to check all projects.',
      ),
    ),
  );
}

function project_release_drush_help($section) {
  switch ($section) {
    case 'meta:project_release:title':
      return dt('Project release commands');
  }
}

function drush_project_release_release_create_history($project_id = NULL) {
  // Force the right umask while this script runs, so that everything is created
  // with sane file permissions.
  umask(0022);

  if (is_null($project_id)) {
    drush_log(dt('Generating XML release history files for all projects.'), 'ok');
  }
  else {
    $project_node = project_load($project_id);
    if ($project_node === FALSE) {
      return drush_set_error('INVALID_PROJECT', dt('Project ID @id not found.', array('@id' => $project_id)));
    }
  }

  if (isset($project_node)) {
    project_release_history_generate_project_xml($project_node);
  }
  else {
    // Generate all xml files for projects with releases.
    $query = new EntityFieldQuery();
    $result = $query->entityCondition('entity_type', 'node')
    ->entityCondition('bundle', project_project_node_types())
    ->fieldCondition('field_project_type', 'value', 'full')
    ->execute();
    if (!empty($result)) {
      foreach ($result['node'] as $project) {
        project_release_history_generate_project_xml(node_load($project->nid, NULL, TRUE));

        // Reset caches so we aren't leaking memory.
        entity_get_controller('field_collection_item')->resetCache();
        backdrop_static_reset('field_language');
        backdrop_lookup_path('wipe');
        foreach (versioncontrol_get_backends() as $backend) {
          $backend->getController('repo')->resetCache();
        }
      }
    }
  }

  // If we're operating on all projects, generate the huge list, too.
  if (is_null($project_id)) {
    $result = db_query('SELECT n.title, n.nid, n.status, n.type, p.name AS machine_name, u.name AS username FROM {node} n INNER JOIN {project} p ON n.nid = p.nid INNER JOIN {users} u ON n.uid = u.uid');
    $xml = '';
    foreach ($result as $project) {
      $xml .= " <project>\n";
      $xml .= '  <title>' . check_plain($project->title) . "</title>\n";
      $xml .= '  <short_name>' . check_plain($project->machine_name) . "</short_name>\n";
      $xml .= '  <link>' . url('node/' . $project->nid, array('absolute' => TRUE)) . "</link>\n";
      $xml .= '  <dc:creator>' . check_plain($project->username) . "</dc:creator>\n";
      $xml .= '  <type>' . check_plain($project->type) . "</type>\n";
      $term_query = db_query('SELECT v.name AS vocab_name, v.vid, td.name AS term_name, td.tid FROM {taxonomy_index} ti INNER JOIN {taxonomy_term_data} td ON ti.tid = td.tid INNER JOIN {taxonomy_vocabulary} v ON td.vid = v.vid WHERE ti.nid = :nid', array(':nid' => $project->nid));
      $xml_terms = '';
      foreach ($term_query as $term) {
        $xml_terms .= '   <term><name>' . check_plain($term->vocab_name) . '</name>';
        $xml_terms .= '<value>' . check_plain($term->term_name) . "</value></term>\n";
      }
      $xml_terms .= _project_release_compatibility_term($project);

      if (!empty($xml_terms)) {
        $xml .= "  <terms>\n" . $xml_terms . "  </terms>\n";
      }
      if (!$project->status) {
        // If it's not published, we can skip the rest for this project.
        $xml .= "  <project_status>unpublished</project_status>\n";
      }
      else {
        $xml .= "  <project_status>published</project_status>\n";
        // Include a list of API terms if available.
        if (!empty($api_vid)) {
          $term_query = db_query('SELECT DISTINCT td.name FROM field_data_field_release_project rp INNER JOIN taxonomy_index ti ON ti.nid = rp.entity_id INNER JOIN taxonomy_term_data td ON ti.tid = td.tid AND td.vid = :api_vid WHERE rp.field_release_project_target_id = :nid ORDER BY td.weight ASC', array(':api_vid' => $api_vid, ':nid' => $project->nid));
          $xml_api_terms = '';
          foreach ($term_query as $api_term) {
            $xml_api_terms .= '   <api_version>' . check_plain($api_term->name) . "</api_version>\n";
          }
          if (!empty($xml_api_terms)) {
            $xml .= "  <api_versions>\n" . $xml_api_terms . "  </api_versions>\n";
          }
        }
      }

      $xml .= " </project>\n";
    }
    if (empty($xml)) {
      return drush_set_error('NO_PROJECTS', dt('No projects found on this server.'));
    }
    return project_release_history_write_xml($xml);
  }
}

/**
 * Generate the XML history file for a given project name and API compatibility
 * term.
 *
 * If a history file already exists for this combination, this function will
 * generate a new history and atomically replace the old one (currently, just
 * logs to watchdog for debugging).
 *
 * @param $project
 *   Project node entity.
 * @param $version_api
 *   The API compatibility to use, or NULL if versions are considered.
 */
function project_release_history_generate_project_xml($project, $version_api = NULL) {
  $vocabularies = taxonomy_vocabulary_load_multiple(FALSE);

  drush_log(dt('Generating release history for @machine_name.', array('@machine_name' => $project->project['name'])), 'ok');

  $releases_xml = array(
    'all' => '',
  );

  if ($project->status) {
    foreach (project_release_query_releases($project->nid, $version_api) as $release) {
      $output = "<release>\n";
      $output .= '  <name>' . check_plain($release->title) . "</name>\n";
      $output .= '  <version>' . check_plain($release->project_release['version']) . "</version>\n";
      foreach (array('major', 'minor', 'patch', 'extra') as $type) {
        $version_type = 'version_' . $type;
        // 0 is a totally legitimate value for any of these version fields, so
        // we need to test with isset() instead of !empty(). However, we do
        // *not* want to include anything in the release history XML if the
        // value is an empty string.
        if (isset($release->project_release['version_' . $version_type]) && $release->project_release['version_' . $version_type] !== '') {
          $output .= "  <$version_type>" . check_plain($release->project_release['version_' . $version_type]) . "</$version_type>\n";
        }
      }

      // Need to fetch list of files for this release
      if (!empty($release->field_release_files[$release->language])) {
        $files = array_map('field_collection_field_get_entity', $release->field_release_files[$release->language]);
      }
      else {
        $files = array();
      }

      if ($release->status) {
        // Published, so we should include the links.
        $output .= "  <status>published</status>\n";
        $output .= '  <release_link>' . url('node/' . $release->nid, array('absolute' => TRUE)) . "</release_link>\n";
        if (!empty($files[0])) {
          $download_link = field_view_value('field_collection_item', $files[0], 'field_release_file', $files[0]->field_release_file[$release->language][0]);
          $output .= '  <download_link>' . $download_link['#file']->uri . "</download_link>\n";
        }
      }
      else {
        $output .= "  <status>unpublished</status>\n";
      }
      // We want to include the rest of these regardless of the status.
      if (!empty($files[0])) {
        $output .= '  <date>' . check_plain($files[0]->field_release_file[$release->language][0]['timestamp']) . "</date>\n";
        $output .= '  <mdhash>'  . check_plain($files[0]->field_release_file_hash[$release->language][0]['value']) . "</mdhash>\n";
        $output .= '  <filesize>' . check_plain($files[0]->field_release_file[$release->language][0]['filesize']) . "</filesize>\n";
      }

      $output .= "  <files>\n";
      foreach ($files as $file) {
        $output .= "   <file>\n";
        if ($release->status) {
          $download_link = field_view_value('field_collection_item', $file, 'field_release_file', $file->field_release_file[$release->language][0]);
          $output .= '    <url>' . $download_link['#file']->uri . "</url>\n";
        }
        $file_parts = explode('.', $file->field_release_file[$release->language][0]['filename']);
        $archive_type = array_pop($file_parts);
        // See if the previous extension is '.tar' and if so, add that, so we
        // see 'tar.gz' or 'tar.bz2' instead of just 'gz' or 'bz2'.
        $previous_ext = array_pop($file_parts);
        if ($previous_ext == 'tar') {
          $archive_type = $previous_ext . '.' . $archive_type;
        }
        else {
          // Put it back on the array, so our profile logic below still works.
          array_push($file_parts, $previous_ext);
        }
        $output .= '    <archive_type>' . $archive_type . "</archive_type>\n";

        /// @todo: This is a drupal.org-specific hack.
        /// @see http://drupal.org/node/1003764
        if ($project->type === 'project_distribution') {
          $variant_chunk = array_pop($file_parts);
          if (strrpos($variant_chunk, 'no-core') !== FALSE) {
            $variant = 'projects';
          }
          elseif (strrpos($variant_chunk, 'core') !== FALSE) {
            $variant = 'full';
          }
          else {
            $variant = 'profile-only';
          }
          $output .= '    <variant>' . $variant . "</variant>\n";
        }
        $output .= '    <md5>' . check_plain($file->field_release_file_hash[$release->language][0]['value']) . "</md5>\n";
        $output .= '    <size>' . check_plain($file->field_release_file[$release->language][0]['filesize']) . "</size>\n";
        $output .= '    <filedate>' . check_plain($file->field_release_file[$release->language][0]['timestamp']) . "</filedate>\n";
        $output .= "   </file>\n";
      }
      $output .= "  </files>\n";

      $security_update = $release->project_release['security_update'];
      $output .= "  <security_update>\n" . ($security_update ? 'TRUE' : 'FALSE') . "  </security_update>\n";

      // Backwards-compatibility: Provide security update information as a
      // "term". Update.module cares *only* about security updates, it has no
      // support for the "New features" or "Bug fixes" terms.
      if (!empty($security_update)) {
        $output .= "  <terms>\n";
        $output .= "    <term><name>Release type</name><value>Security update</value></term>\n";
        $output .= "  </terms>\n";
      }

      $output .= "</release>\n";

      $releases_xml['all'] .= $output;
      if (isset($releases_xml[$api_tid])) {
        $releases_xml[$api_tid] .= $output;
      }
      else {
        $releases_xml[$api_tid] = $output;
      }
    }
  }

  foreach ($releases_xml as $api_tid => $release_xml) {
    $xml = '<title>' . check_plain($project->title) . "</title>\n";
    $xml .= '<short_name>' . check_plain($project->project['name']) . "</short_name>\n";
    $xml .= '<dc:creator>' . check_plain($project->name) . "</dc:creator>\n";
    $xml .= '<type>' . check_plain($project->type) . "</type>\n";

    if ($api_tid === 'all') {
      $api_version = 'all';
    }
    else {
      $api_term = taxonomy_term_load($api_tid);
      if ($api_term === FALSE) {
        continue;
      }
      $api_version = check_plain($api_term->name);
    }
    $xml .= '<api_version>' . $api_version . "</api_version>\n";

    if ($project->status) {
      $project_status = 'published';
      if ($api_tid !== 'all') {
        // Include the info about supported and recommended major versions.
        $query = db_query('SELECT major, supported, recommended FROM {project_release_supported_versions} WHERE nid = :nid AND tid = :tid AND (supported = 1 OR recommended = 1)', array(':nid' => $project->nid, ':tid' => $api_tid));
        $supported_majors = array();
        $recommended_major = NULL;
        foreach ($query as $version_info) {
          if ($version_info->supported) {
            $supported_majors[] = $version_info->major;
          }
          if ($version_info->recommended) {
            $recommended_major = $version_info->major;
          }
        }
        if (isset($recommended_major)) {
          $xml .= '<recommended_major>' . $recommended_major . "</recommended_major>\n";
        }
        if (empty($supported_majors)) {
          $project_status = 'unsupported';
        }
        else {
          $xml .= '<supported_majors>' . implode(',', $supported_majors) . "</supported_majors>\n";
          // To avoid confusing existing clients, include <default_major>, too.
          $xml .= '<default_major>' . min($supported_majors) . "</default_major>\n";
        }
      }
      $xml .= '<project_status>' . $project_status . "</project_status>\n";
      $xml .= '<link>' . url('node/' . $project->nid, array('absolute' => TRUE)) . "</link>\n";

      // To prevent the update(_status) module from having problems parsing the
      // XML, the terms need to be at the end of the information for the project.
      $tids = array();
      foreach (taxonomy_get_vocabularies() as $vocabulary) {
        if (isset($project->{'taxonomy_' . $vocabulary->machine_name}[$project->language])) {
          foreach ($project->{'taxonomy_' . $vocabulary->machine_name}[$project->language] as $term) {
            $tids[] = $term['tid'];
          }
        }
      }
      if (!empty($tids)) {
        $xml .= "  <terms>\n";
        $xml .= _project_release_compatibility_term($project);
        foreach (taxonomy_term_load_multiple($tids) as $term) {
          $xml .= '   <term><name>' . check_plain($vocabularies[$term->vocabulary_machine_name]->name) . '</name>';
          $xml .= '<value>' . check_plain($term->name) . "</value></term>\n";
        }
        $xml .= "  </terms>\n";
      }
    }
    else {
      $xml .= "<project_status>unpublished</project_status>\n";
    }

    if (!empty($release_xml)) {
      $xml .= "<releases>\n" . $release_xml . "</releases>\n";
    }

    project_release_history_write_xml($xml, $project, $api_version);
  }
}

/**
 * Add backward compatibility project type term.
 *
 * Drupal.org-specific hack, see https://drupal.org/node/2126123. Drush
 * expects taxonomy terms which no longer exist on Drupal.org.
 */
function _project_release_compatibility_term($project) {
  $extra_terms = array(
    'project_core' => "   <term><name>Projects</name><value>Backdrop core</value></term>\n",
    'project_distribution' => "   <term><name>Projects</name><value>Distributions</value></term>\n",
    'project_module' => "   <term><name>Projects</name><value>Modules</value></term>\n",
    'project_theme' => "   <term><name>Projects</name><value>Themes</value></term>\n",
    'project_theme_engine' => "   <term><name>Projects</name><value>Theme engines</value></term>\n",
    'project_translation' => "   <term><name>Projects</name><value>Translations</value></term>\n",
  );

  return isset($extra_terms[$project->type]) ? $extra_terms[$project->type] : '';
}

/**
 * Write out the XML history for a given project and version to a file.
 *
 * @param $xml
 *   String containing the XML representation of the history.
 * @param $project
 *   An object containing (at least) the title and project attributes.
 * @param $api_version
 *   The API compatibility version the history is for.
 */
function project_release_history_write_xml($xml, $project = NULL, $api_version = NULL) {
  // Dublin core namespace according to http://dublincore.org/documents/dcmi-namespace/
  $dc_namespace = 'xmlns:dc="http://purl.org/dc/elements/1.1/"';
  $full_xml = '<?xml version="1.0" encoding="utf-8"?>' . "\n";
  if (is_null($project)) {
    // We are outputting a global project list.
    $project_dir = 'project-list';
    $filename = 'project-list-all.xml';
    $full_xml .= '<projects ' . $dc_namespace . ">\n" . $xml . "</projects>\n";
  }
  else {
    // Setup the filenames we'll be using.
    $project_dir = $project->project['name'];
    $filename = $project->project['name'] . '-' . strtr($api_version, '/', '_') . '.xml';
    $full_xml .= '<project ' . $dc_namespace . ">\n" . $xml . "</project>\n";
  }

  // Make sure we've got the right project-specific subdirectory.
  $project_dir = file_build_uri(settings_get('project_release_history_root', 'release-history') . '/' . $project_dir);
  if (!file_prepare_directory($project_dir, FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
    $wrapper = file_stream_wrapper_get_instance_by_uri($project_dir);
    return drush_set_error('NO_DIRECTORY', dt('@path does not exist or is not writable.', array('@path' => $wrapper->realpath())));
  }

  // Write out file.
  if (!file_unmanaged_save_data($full_xml, $project_dir . '/' . $filename, FILE_EXISTS_REPLACE)) {
    $wrapper = file_stream_wrapper_get_instance_by_uri($project_dir . '/' . $filename);
    return drush_set_error('FILE_WRITE', dt('Can\'t write to @file.', array('@file' => $wrapper->realpath())));
  }
}

/**
 * Check status (recommended, latest, update status, etc) for releases.
 */
function drush_project_release_release_check_status($project_id = NULL) {
  if (is_null($project_id)) {
    drush_log(dt('Checking release status for all projects.'), 'ok');
    project_release_release_check_status_all();
  }
  else {
    $project_node = project_load($project_id);
    if ($project_node === FALSE) {
      return drush_set_error('INVALID_PROJECT', dt('Project ID @id not found.', array('@id' => $project_id)));
    }
    else {
      drush_log(dt('Checking release status for @title [@machine_name]', array('@title' => $project_node->title, '@machine_name' => $project_node->project['name'])), 'ok');
    }
    project_release_release_check_status_project($project_node);
  }
}

/**
 * Check release status for all branches of all projects.
 *
 * Queries {project_release_supported_versions} for all branches across all
 * projects, groups the branches per-project, and invokes a helper function,
 * project_release_compute_update_status_multiple(), to actually compute the
 * update status of every release on each branch, update the release download
 * table (once per project), etc.
 *
 * @see project_release_compute_update_status_multiple()
 */
function project_release_release_check_status_all() {
  $results = db_query("SELECT nid, tid, major FROM {project_release_supported_versions} WHERE supported = 1");
  $branch_count = 0;
  $project_count = 0;
  $branches = array();
  $current_nid = 0;
  foreach ($results as $branch) {
    $branch_count++;
    if ($current_nid != $branch->nid) {
      if (!empty($branches)) {
        project_release_compute_update_status_multiple($current_nid, $branches);
        $project_count++;
        $branches = array();
      }
      $current_nid = $branch->nid;
    }
    $branches[] = array('api_tid' => $branch->tid, 'major' => $branch->major);
  }
  project_release_compute_update_status_multiple($current_nid, $branches);
  $project_count++;
  drush_log(dt('Checked release status for @branch_count branches across @project_count projects.', array('@branch_count' => $branch_count, '@project_count' => $project_count)), 'ok');
}

/**
 * Check the release status on a single project.
 *
 * Queries {project_release_supported_versions} for all branches for a single
 * project and invokes project_release_compute_update_status_multiple() once
 * to compute the release update status, rebuild the download table, etc.
 *
 * @see project_release_compute_update_status_multiple()
 */
function project_release_release_check_status_project($project) {
  $results = db_query("SELECT tid, major FROM {project_release_supported_versions} WHERE supported = 1 AND nid = :nid", array(':nid' => $project->nid));
  $branches = array();
  foreach ($results as $branch) {
    $branches[] = array('api_tid' => $branch->tid, 'major' => $branch->major);
  }
  project_release_compute_update_status_multiple($project->nid, $branches);
  drush_log(dt('Checked release status for @count branches.', array('@count' => count($branches))), 'ok');
}
