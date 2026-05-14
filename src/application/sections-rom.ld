/* Linker section snippet for Zephyr HTTP server resources.
 *
 * Defines the symbols expected by HTTP_SERVICE_DEFINE(test_http_service,...):
 *   _http_resource_desc_test_http_service_list_start/_end
 *
 * Match the actual input section names produced by HTTP_RESOURCE_DEFINE().
 */

.http_resource_desc_test_http_service : ALIGN(4)
{
    _http_resource_desc_test_http_service_list_start = .;

    /* Input sections as seen in zephyr.map:
     *   ._http_resource_desc_test_http_service.static.<name>_
     *   ._http_resource_desc_test_http_service.dynamic.<name>_
     *   ._http_resource_desc_test_http_service.websocket.<name>_
     */
    KEEP(*(SORT_BY_ALIGNMENT(._http_resource_desc_test_http_service.static.*)))
    KEEP(*(SORT_BY_ALIGNMENT(._http_resource_desc_test_http_service.dynamic.*)))
    KEEP(*(SORT_BY_ALIGNMENT(._http_resource_desc_test_http_service.websocket.*)))

    _http_resource_desc_test_http_service_list_end = .;
}