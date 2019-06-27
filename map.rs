use std::boxed::Box;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;

type Map = HashMap<CString, *mut u8>;

// A hashmap from string to pointer.
#[no_mangle]
pub extern "C" fn init_hash_map() -> *mut Map {
    let map = Box::new(HashMap::<CString, *mut u8>::new());
    Box::into_raw(map)
}

#[no_mangle]
pub extern "C" fn hash_map_get(map: *mut Map, key: *const c_char) -> *mut u8 {
    let map = unsafe { Box::from_raw(map) };
    let key = unsafe { CString::new(CStr::from_ptr(key).to_bytes()).unwrap() };
    let ret = map.get(&key).map_or(ptr::null_mut(), |&v| v);
    Box::into_raw(map);
    ret
}

#[no_mangle]
pub extern "C" fn hash_map_insert(map: *mut Map, key: *const c_char, value: *mut u8) -> *mut u8 {
    let mut map = unsafe { Box::from_raw(map) };
    let key = unsafe { CString::new(CStr::from_ptr(key).to_bytes()).unwrap() };
    let ret = map.insert(key, value).unwrap_or(ptr::null_mut());
    Box::into_raw(map);
    ret
}

#[no_mangle]
pub extern "C" fn hash_map_remove(map: *mut Map, key: *const c_char) -> *mut u8 {
    let mut map = unsafe { Box::from_raw(map) };
    let key = unsafe { CString::new(CStr::from_ptr(key).to_bytes()).unwrap() };
    let ret = map.remove(&key).unwrap_or(ptr::null_mut());
    Box::into_raw(map);
    ret
}

// TODO: pass in a callback to deconstruct values.
#[no_mangle]
pub extern "C" fn free_map(map: *mut Map, free_value: fn(*mut u8)) {
    let map = unsafe { Box::from_raw(map) };
    for (_, value) in map.into_iter() {
        if !(free_value as *const ()).is_null() {
            free_value(value);
        }
    }
}

#[repr(C)]
#[no_mangle]
pub struct CMap;
