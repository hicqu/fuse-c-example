use std::boxed::Box;
use std::collections::HashMap;
use std::ffi::CStr;
use std::ptr;

type Map = HashMap<&'static CStr, *mut u8>;

// A hashmap from string to pointer.
#[no_mangle]
pub extern "C" fn init_hash_map() -> *mut Map {
    let map = Box::new(HashMap::<&'static CStr, *mut u8>::new());
    Box::into_raw(map)
}

#[no_mangle]
pub extern "C" fn hash_map_get(map: *mut Map, key: &'static CStr) -> *mut u8 {
    let map = unsafe { Box::from_raw(map) };
    let ret = map.get(key).map_or(ptr::null_mut(), |v| *v);
    Box::into_raw(map);
    ret
}

#[no_mangle]
pub extern "C" fn hash_map_insert(map: *mut Map, key: &'static CStr, value: *mut u8) -> *mut u8 {
    let mut map = unsafe { Box::from_raw(map) };
    let ret = map.insert(key, value).unwrap_or(ptr::null_mut());
    Box::into_raw(map);
    ret
}

#[no_mangle]
pub extern "C" fn hash_map_remove(map: *mut Map, key: &'static CStr) -> *mut u8 {
    let mut map = unsafe { Box::from_raw(map) };
    let ret = map.remove(&key).unwrap_or(ptr::null_mut());
    Box::into_raw(map);
    ret
}

// TODO: pass in a callback to deconstruct keys and values.
#[no_mangle]
pub extern "C" fn free_map(map: *mut Map, free_key: fn(&'static CStr), free_value: fn(*mut u8)) {
    let map = unsafe { Box::from_raw(map) };
    for (key, value) in map.into_iter() {
        if !(free_key as *const ()).is_null() {
            free_key(key);
        }
        if !(free_value as *const ()).is_null() {
            free_value(value);
        }
    }
}

#[repr(C)]
#[no_mangle]
pub struct CMap;
