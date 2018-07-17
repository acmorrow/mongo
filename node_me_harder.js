Module = require('./libmongo_embedded_capi.js')

m_e_c_v1_status_create = Module.cwrap(
    'mongo_embedded_v1_status_create',
    'number',
    []
)

m_e_c_v1_status_get_explanation = Module.cwrap(
    'mongo_embedded_v1_status_get_explanation',
    'string',
    ['number']
)

m_e_c_v1_lib_init = Module.cwrap(
    'mongo_embedded_v1_lib_init',
    'number',
    ['number', 'number']
)

m_e_c_v1_instance_create = Module.cwrap(
    'mongo_embedded_v1_instance_create',
    'number',
    ['number', 'string', 'number']
)

m_e_c_v1_client_create = Module.cwrap(
    'mongo_embedded_v1_client_create',
    'number',
    ['number', 'number']
)

m_e_c_v1_client_invoke = Module.cwrap(
    'mongo_embedded_v1_client_invoke',
    'number',
    ['number', 'array', 'number', 'number', 'number']
)

m_e_c_v1_client_invoke_helper = function(client, request_uint8_array, status) {
    // Need two 32-bit quantities to hold out parameters
    var out_data_ptr = Module._malloc(8)
    var out_data_array = new Uint32Array(Module.HEAPU32.buffer, out_data_ptr, 2)
    result = m_e_c_v1_client_invoke(client, request_uint8_array, request_uint8_array.length, out_data_ptr, out_data_ptr + 4, status)
    if (result != 0) {
        Module._free(out_data_ptr)
        return null
    }
    var response_uint8_array = new Uint8Array(Module.HEAPU8.buffer, out_data_array[0], out_data_array[1])
    Module._free(out_data_ptr)
    return response_uint8_array
}

status = m_e_c_v1_status_create()

capi_lib = m_e_c_v1_lib_init(0, status)
if (capi_lib == 0) {
    throw m_e_c_v1_status_get_explanation(status)
}

instance_json = "{ 'storage' : { 'dbPath' : '/' }, 'systemLog' : { 'verbosity' : 1 } }"

instance = m_e_c_v1_instance_create(capi_lib, instance_json, status)
if (instance == 0) {
    throw m_e_c_v1_status_get_explanation(status)
}

client = m_e_c_v1_client_create(instance, status)
if (client == 0) {
    throw m_e_c_v1_status_get_explanation(status)
}

// OP_MSG insert of empty doc to collection 'foo'
var insert_request = new Uint8Array([-117, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, -35, 7, 0, 0, 0, 0, 0, 0, 1, 36, 0, 0, 0, 100, 111, 99, 117, 109, 101, 110, 116, 115, 0, 22, 0, 0, 0, 7, 95, 105, 100, 0, 91, 77, -16, -99, -79, 86, -4, -6, 102, 68, 15, 94, 0, 0, 81, 0, 0, 0, 2, 105, 110, 115, 101, 114, 116, 0, 4, 0, 0, 0, 102, 111, 111, 0, 8, 111, 114, 100, 101, 114, 101, 100, 0, 1, 3, 108, 115, 105, 100, 0, 30, 0, 0, 0, 5, 105, 100, 0, 16, 0, 0, 0, 4, -114, -5, -24, -99, 72, -127, 72, -113, -113, 61, 9, 52, 100, -15, 83, 18, 0, 2, 36, 100, 98, 0, 5, 0, 0, 0, 116, 101, 115, 116, 0, 0])

console.log("XXX Invoking embedded with wire protocol bytes", insert_request.toString())

result = m_e_c_v1_client_invoke_helper(client, insert_request, status)
if (result == null) {
    throw m_e_c_v1_status_get_explanation(status)
}

console.log("XXX Embedded invocation responded with wire protocol bytes", result.toString())
