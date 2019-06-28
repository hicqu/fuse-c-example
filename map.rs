use std::boxed::Box;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::{alloc, mem, ptr};

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
pub extern "C" fn clear_map(map: *mut Map, free_value: fn(*mut u8)) {
    let mut map = unsafe { Box::from_raw(map) };
    let taked_map = mem::replace(map.as_mut(), HashMap::new());
    Box::into_raw(map);
    for (_, value) in taked_map {
        if !(free_value as *const ()).is_null() {
            free_value(value);
        }
    }
}

#[no_mangle]
pub extern "C" fn map_next_key(map: *mut Map) -> *mut c_char {
    let map = unsafe { Box::from_raw(map) };
    if let Some(key) = map.keys().next() {
        let s = key.as_bytes_with_nul();
        let ptr = unsafe {
            let layout = alloc::Layout::from_size_align(s.len(), 1).unwrap();
            let p = alloc::alloc(layout);
            ptr::copy_nonoverlapping(&s[0], p, s.len());
            p
        };
        Box::into_raw(map);
        return ptr as *mut c_char;
    }
    Box::into_raw(map);
    ptr::null_mut()
}

#[no_mangle]
pub extern "C" fn destroy_map(map: *mut Map) {
    let map = unsafe { Box::from_raw(map) };
    assert!(map.is_empty());
}

#[repr(C)]
#[no_mangle]
pub struct CMap;
