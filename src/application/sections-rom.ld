/*
 * Custom linker section for Zephyr HTTP server resource registration.
 *
 * Zephyr's HTTP_RESOURCE_DEFINE() macro places resource descriptors into
 * named input sections (._http_resource_desc_<service>.<type>.<name>_).
 * This snippet collects those sections and defines _start/_end symbols
 * so the HTTP server can iterate over registered resources at runtime.
 *
 * The service name "test_http_service" must match the first argument
 * of HTTP_SERVICE_DEFINE().
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