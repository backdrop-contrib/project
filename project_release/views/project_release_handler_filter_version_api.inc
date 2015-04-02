<?php
/**
 * Filter to provide a list of API versions.
 */
class project_release_handler_filter_version_api extends views_handler_filter_in_operator {
  var $value_form_type = 'select';

  function get_value_options() {
    $options = project_release_get_api_versions();
    $this->value_options = backdrop_map_assoc($options);

    return $this->value_options;
  }
}
