<?php

/**
 * Determine whether an API response is an error.
 * If this function returns TRUE, you have an error!
 * @param bool | Emfl_Response $response
 * @return bool
 */
function emfl_response_is_error( $response ) {
  if(empty( $response )) return TRUE;
  if( get_class( $response ) != 'Emfl_Response' ) return TRUE;
  if( $response->success == 0 ) return TRUE;
  return FALSE;
}

/**
 * A wrapper library for the emfluence Marketing Platform.
 * Function names correspond to API endpoints.
 * Refer to http://apidocs.emailer.emfluence.com
 * TODO: To make this CMS-independent, remove the Drupal functions from call()
 * @author jschwartz
 */
class Emfl_Platform_API {

  CONST API_URL = 'https://api.emailer.emfluence.com/v0';

  private $apikey;
  public $timeout = 15; // Low so that if the Platform API is slow, it doesn't cause this script to time out.
  
  /**
   * The function or method to call when an error occurs.
   * You can either pass the string function name or 
   * an array containing the object and method name to be called:
   * for example, array($obj, $methodname)
   * @var string | array
   */
  var $error_function;

  /**
   * Provides an internal storage for errors produced by this api.
   * This will be used if an error function is not provided
   */
  var $errors = NULL;

  /**
   * @param string $api_key
   * @param string $error_function Function name. 
   * The function gets passed any error message as a string parameter.
   */
  function __construct( $api_key, $error_function = NULL ) {
    $this->apikey = $api_key;

    if( !empty($error_function) ) {
      $this->error_function = $error_function;
    } else {
      $this->errors = new Emfl_Platform_API_Errors();
      $this->error_function = array($this->errors, 'add');
    }
  }

  /**
   * Use the error function or method passed in the constructor.
   * @param string $msg
   */
  private function err( $msg ) {
    $func = $this->error_function;
    if( empty($func) || !is_callable($func) ) return; 
    if( is_string($func) ) {
      $func( $msg );
    } elseif( is_array( $func ) ) {
      $obj = $func[0];
      $method = $func[1];
      $obj->$method($msg);
    }
  }

  /**
   * Make a request using the API
   *
   * Return value is FALSE if the request is blocked or unsuccessful in transmission.
   * In this case, the error function passed in the constructor is given an explanation.
   *
   * If communication with the Platform happened, return object is an Emfl_Response object.
   * Bad API calls that get a response from the Platform will have properties that can be
   * inspected like 'status', 'code' and 'errors'. If the response has a bad status,
   * the error function passed in the constructor is given an explanation.
   *
   * This is mostly an internal method, as successful response data should be
   * converted to an object of some type.
   *
   * @param string $endpoint The API endpoint, not beginning with a slash.
   * Eg, 'helper/ping' would be an endpoint.
   * @param array $params Per the API documentation.
   * @return boolean | Emfl_Response
   */
  function call( $endpoint, $params ) {

    // make the call with the best request handler available
    $url = self::API_URL . '/' . $endpoint;
    $params['apiKey'] = $this->apikey;

    if( function_exists('drupal_http_request') && defined('VERSION') ) {
      // Drupal request handler
      if( VERSION > 7 ) {
        $response = drupal_http_request(
            $url,
            array(
              'headers' => array( 'Content-Type' => 'application/json' ),
              'method' => 'POST',
              'data' => json_encode( (object) $params),
              'timeout' => $this->timeout
                )
        );
      } else {
        $response = drupal_http_request(
            $url,
            array( 'Content-Type' => 'application/json' ),
            'POST',
            json_encode( (object) $params),
            1,
            $this->timeout
        );
      }

    } elseif( function_exists('wp_remote_post') && false ) {
      // Wordpress request handler
      $response = wp_remote_post(
      	$url,
        array(
          'timeout' => $this->timeout,
        	'headers' => array( array( 'Content-Type' => 'application/json' ) ),
          'body' => json_encode( (object) $params )
        )
      );    dbgout($response);

      if( !is_wp_error($response) ) {
        $response = (object) $response['response'];
        $response->data = $response->body;
      }

    } elseif( function_exists('curl_init') ) {
      // Fall back on CURL
      $curl = curl_init($url);
      curl_setopt($curl, CURLOPT_HEADER, false);
      curl_setopt($curl, CURLOPT_RETURNTRANSFER, true);
      curl_setopt($curl, CURLOPT_HTTPHEADER, array("Content-type: application/json"));
      curl_setopt($curl, CURLOPT_CONNECTTIMEOUT, 2);
      curl_setopt($curl, CURLOPT_TIMEOUT, $this->timeout);
      curl_setopt($curl, CURLOPT_POST, true);
      curl_setopt($curl, CURLOPT_POSTFIELDS, json_encode( (object) $params ) );
      $json_response = curl_exec($curl);
      $status = curl_getinfo($curl, CURLINFO_HTTP_CODE);
      curl_close($curl);
      $response = (object) array(
      	'data' => $json_response,
        'code' => $status
      );

    } else {
      $this->err( 'No request handler could be found. Please install CURL on your server.' );
      return FALSE;
    }

    // Look for transmission fail
    if( !isset($response->code) || ($response->code != 200) ) {
      $this->err( 'Transmission failure ' . var_export($response, TRUE) );
      return FALSE;
    }

    // Focus on the response content
    require_once 'response_objects/wrapper.class.inc';
    $response = json_decode($response->data);
    $response = new Emfl_Response($response);

    // Look for API fail
    if($response->success != 1) {
      $this->err( 'Bad API call: ' . $response->code . ' - ' . var_export($response->errors, TRUE) );
    }

    return $response;
  }

  
  //////////
  // Test //
  //////////
  
  /**
   * Test method, use it to see whether API key,
   * error_function and request handlers are working.
   *
   * @see http://apidocs.emailer.emfluence.com/#responses
   * @return boolean | Emfl_Response
   */
  function ping() {
    return $this->call( 'helper/ping', array() );
  }

  
  //////////////
  // Contacts //
  //////////////
  
  /**
   * Save a contact and get the saved contact in return.
   *
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'data'
   * property will be FALSE and the 'errors' property will have details.
   *
   * Finally, if the operation was successful, the 'data' property is a
   * populated Emfl_Contact object for the saved contact.
   *
   * @param array $params Per the API reference.
   * @return bool | Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/contacts/save
   */
  function contacts_save( $params ) {
    $response = $this->call('contacts/save', $params);
    if(empty($response)) return FALSE; // Transmission error
    require_once 'response_objects/contact.class.inc';
    if(!empty( $response->data )) $response->data = new Emfl_Contact($response->data);
    return $response;
  }

  /**
   * Import / save multiple contacts
   * 
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'data'
   * property will be FALSE and the 'errors' property will have details.
   *
   * Finally, if the operation was successful, the 'data' property is a
   * populated Emfl_Contacts_Import object with summary details.
   *
   * @param array $params Per the API reference.
   * @return bool | Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/contacts/import
   */
  function contacts_import( $params ) {
    $response = $this->call('contacts/import', $params);
    if(empty($response)) return FALSE; // Transmission error
    require_once 'response_objects/contacts.import.class.inc';
    if(!empty( $response->data )) $response->data = new Emfl_Contacts_Import($response->data);
    return $response;
  }

  ////////////
  // Groups //
  ////////////

  /**
   * Groups / search
   *
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'data'
   * property will be FALSE and the 'errors' property will have details.
   *
   * Finally, if the operation was successful, the 'data' property is a
   * populated Emfl_Group_Search object with paging details and a records
   * list of groups. Note however that the populated group objects are not
   * complete group objects, as per the API documentation.
   *
   * @param array $params Keyed values, per API documentation
   * @return Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/groups/search
   */
  function groups_search( $params = array() ) {
    $response = $this->call('groups/search', $params);
    if(empty($response)) return FALSE; // Transmission error
    if(!empty($response->data)) {
      require_once 'response_objects/groups.search.class.inc';
      $response->data = new Emfl_Groups_Search($response->data->paging, $response->data->records);
    }
    return $response;
  }
  
  ////////////
  // Emails //
  ////////////
  
  /**
   * Emails / search
   *
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'data'
   * property will be FALSE and the 'errors' property will have details.
   *
   * Finally, if the operation was successful, the 'data' property is a
   * populated Emfl_Email_Search object with paging details and a records 
   * list of emails. Note however that the populated email objects are not 
   * complete email objects, as per the API documentation.
   *
   * @param array $params Keyed values, per API documentation
   * @return Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/emails/copy
   */
  function emails_search( $params = array() ) {
    $response = $this->call('emails/search', $params);
    if(empty($response)) return FALSE; // Transmission error
    if(!empty($response->data)) {
      require_once 'response_objects/emails.search.class.inc';
      $response->data = new Emfl_Emails_Search($response->data->paging, $response->data->records);
    }
    return $response;
  }
  
  /**
   * Emails / lookup
   * 
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'data'
   * property will be FALSE and the 'errors' property will have details.
   *
   * Finally, if the operation was successful, the 'data' property is a
   * populated Emfl_Email object with email details.
   * 
   * @param int $email_id The email ID
   * @return boolean| Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/emails/lookup
   */
  function emails_lookup( $email_id ) {
    $response = $this->call('emails/lookup', array('emailID' => $email_id));
    if(empty($response)) return FALSE; // Transmission error
    require_once 'response_objects/email.class.inc';
    if(!empty( $response->data )) $response->data = new Emfl_Email($response->data);
    return $response;
  }
  
  /**
   * Emails / save
   * 
   * Note that this method does NOT save all possible fields of an email object.
   * 
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'data'
   * property will be FALSE and the 'errors' property will have details.
   *
   * Finally, if the operation was successful, the 'data' property is a
   * populated Emfl_Email object with email details.
   * 
   * @param Emfl_Email $email It's probably best to use a response object from an emails_lookup call as the input here.
   * @return Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/emails/save
   */
  function emails_save( Emfl_Email $email ) {
    $response = $this->call('emails/save', (array) $email );
    if(empty($response)) return FALSE; // Transmission error
    require_once 'response_objects/email.class.inc';
    if(!empty( $response->data )) $response->data = new Emfl_Email($response->data);
    return $response;
  }
  
  /**
   * Emails / copy
   *
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'data'
   * property will be FALSE and the 'errors' property will have details.
   *
   * Finally, if the operation was successful, the 'data' property is a
   * populated Emfl_Email object with email details.
   *
   * @param int $email_id The email ID
   * @return Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/emails/copy
   */
  function emails_copy( $email_id ) {
    $response = $this->call('emails/copy', array('emailID' => $email_id));
    if(empty($response)) return FALSE; // Transmission error
    require_once 'response_objects/email.class.inc';
    if(!empty( $response->data )) $response->data = new Emfl_Email($response->data);
    return $response;
  }
  
  /**
   * Emails / schedule
   *
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'errors' 
   * property will have details.
   *
   * @param int $email_id The email ID
   * @param string $schedule_send_time Future date when email should be sent, in GMT. Format: Y-m-d H:i:s
   * @return Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/emails/copy
   */
  function emails_schedule( $email_id, $schedule_send_time ) {
    $response = $this->call('emails/schedule', array(
        'emailID' => $email_id, 
        'scheduleSendTime' => $schedule_send_time
      ));
    if(empty($response)) return FALSE; // Transmission error
    return $response;
  }
  
  /**
   * Emails / sendTest
   *
   * The return value could be FALSE if a transmission error occurred,
   * like being blocked by the Platform or a network issue.
   *
   * Otherwise even bad API calls will get an Emfl_Response object
   * that corresponds with the Platform's response format. See here:
   * http://apidocs.emailer.emfluence.com/#responses
   *
   * Don't forget to check the 'success' property before assuming that
   * the operation occurred correctly. If an error occurred, the 'errors' 
   * property will have details.
   *
   * @param int $email_id The email ID
   * @param string $recipient Email address to send the test to.
   * @return Emfl_Response
   * @see https://apidocs.emailer.emfluence.com/endpoints/emails/sendTest
   */
  function emails_sendTest( $email_id, $recipient ) {
    $response = $this->call('emails/sendTest', array(
        'emailID' => $email_id,
        'recipientEmail' => $recipient
    ));
    if(empty($response)) return FALSE; // Transmission error
    return $response;
  }
  
}

/**
 * Class Emfl_Platform_API_Errors
 * Provides basic error storage and retrieval
 */
class Emfl_Platform_API_Errors {
  protected $errors = array();

  /**
   * Adds an error to the list
   * @param $error
   */
  function add($error){
    $this->errors[] = $error;
  }

  /**
   * Retrieves all errors
   * @param boolean $clear = FALSE
   * @return array
   */
  function get($clear = FALSE){
    $errors = $this->errors;
    if( $clear ){
      $this->clear();
    }
    return $errors;
  }

  /**
   * Retrieves the last error
   * @param boolean $clear = FALSE
   * @return string
   */
  function get_last($clear = FALSE){
    $errors = $this->errors;
    if( $clear ){
      $this->clear();
    }
    return array_pop($errors);
  }

  /**
   * Empties the error list
   */
  function clear(){
    $this->errors = array();
  }
}