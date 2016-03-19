<?php
/**
 * @file
 * Documentation for Project GitHub hooks and callbacks.
 */

/**
 * Validate and optionally modify a project node before automated creation.
 *
 * This hook may be used to prevent the creation of a node when a new project
 * release is created on GitHub. To prevent creation, add an entry to the
 * $errors array, that is passed by reference.
 *
 * The $project_node is a full Node object that has not yet been created. You
 * may modify it in any way, including changing the author of the node or even
 * its type (if you have multiple project types on your site).
 *
 * @param Node $project_node
 *   The project node about to be created.
 * @param array $errors
 *   An empty array of errors that is passed by reference. If the creation of
 *   this node should be prevented, add an entry to this array.
 * @param array $payload
 *   The payload from the GitHub create release event. See
 *   https://developer.github.com/v3/activity/events/types/#releaseevent
 * @return NULL
 *   No return value.
 */
function hook_github_project_validate(Node $project_node, array &$errors, object $payload) {
  if (strpos($payload->repository->full_name, 'module')) {
    $project_node->type = 'project_module';
  }
  else {
    $errors['name_invalid'] = t('Project must contain the string "module" in the project name.');
  }
}

/**
 * Validate and optionally modify a release node before automated creation.
 *
 * This hook may be used to prevent the creation of a node when a new project
 * release is created on GitHub. To prevent creation, add an entry to the
 * $errors array, that is passed by reference.
 *
 * The $release_node is a full Node object that has not yet been created. You
 * may modify it in any way, including changing the author of the node or even
 * its type (if you have multiple project types on your site).
 *
 * This hook will only be fired if a parent project node has also been created
 * or already existed, therefor you do not have to re-validate the value within
 * $node->project_release['project_nid'].
 *
 * @param Node $release_node
 *   The release node about to be created.
 * @param array $errors
 *   An empty array of errors that is passed by reference. If the creation of
 *   this node should be prevented, add an entry to this array.
 * @param array $payload
 *   The payload from the GitHub create release event. See
 *   https://developer.github.com/v3/activity/events/types/#releaseevent
 * @return NULL
 *   No return value.
 */
function hook_github_project_validate_release(Node $release_node, array &$errors, object $payload) {
  $project_node = node_load($release_node->project['release_nid']);
  if ($project_node->type === 'project_module') {
    $release_node->type = 'module_release';
  }
  else {
    $errors['project_type_invalid'] = t('Project releases are only supported on module projects.');
  }
}
