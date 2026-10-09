;;;; Chat UI DTK WASM Plugin: chat_ui_dtk.wat
;;;
;;; This plugin IMPORTS DTK host functions. It cannot be instantiated
;;; without a DTK host that provides the "dtk" module imports.
;;;
;;; The build_ui() function calls DTK API to create widgets:
;;;   dtk_set_window_title, dtk_set_window_size,
;;;   dtk_create_label, dtk_create_textedit, dtk_create_lineedit,
;;;   dtk_create_pushbutton, dtk_create_hbox, dtk_add_to_layout
;;;
;;; The format_message(ptr, len) function uses dtk_append_message
;;; to display formatted messages.

(module
  ;; DTK host imports — the plugin DEPENDS on these being available.
  ;; Without a DTK host, the module cannot be instantiated.
  (import "dtk" "dtk_create_label"      (func $dtk_create_label (param i32 i32) (result i32)))
  (import "dtk" "dtk_create_textedit"   (func $dtk_create_textedit (param i32 i32) (result i32)))
  (import "dtk" "dtk_create_lineedit"   (func $dtk_create_lineedit (param i32 i32) (result i32)))
  (import "dtk" "dtk_create_pushbutton" (func $dtk_create_pushbutton (param i32 i32) (result i32)))
  (import "dtk" "dtk_create_hbox"       (func $dtk_create_hbox (param i32 i32) (result i32)))
  (import "dtk" "dtk_add_to_layout"     (func $dtk_add_to_layout (param i32 i32) (result i32)))
  (import "dtk" "dtk_set_window_title"  (func $dtk_set_window_title (param i32 i32) (result i32)))
  (import "dtk" "dtk_set_window_size"   (func $dtk_set_window_size (param i32 i32) (result i32)))
  (import "dtk" "dtk_get_theme"          (func $dtk_get_theme (result i32)))
  (import "dtk" "dtk_set_theme"          (func $dtk_set_theme (param i32 i32) (result i32)))
  (import "dtk" "dtk_append_message"     (func $dtk_append_message (param i32 i32) (result i32)))

  (memory (export "memory") 1)

  ;; String data section — all UI strings stored in linear memory
  (data (i32.const 512) "WASM-Powered Chat (DTK)")  ;; [0] "WASM-Powered Chat (DTK)"
  (data (i32.const 536) "WASM-Powered Chat Client (DTK)")  ;; [1] "WASM-Powered Chat Client (DTK)"
  (data (i32.const 567) "Initializing...")  ;; [2] "Initializing..."
  (data (i32.const 583) "Type your message here...")  ;; [3] "Type your message here..."
  (data (i32.const 609) "Send")  ;; [4] "Send"
  (data (i32.const 614) "Messages will appear here...")  ;; [5] "Messages will appear here..."
  (data (i32.const 643) "[Me] ")  ;; [6] "[Me] "
  (data (i32.const 649) "[System] ")  ;; [7] "[System] "

  (global $heap_ptr (mut i32) (i32.const 659))

  (func $alloc (export "alloc") (param $size i32) (result i32)
    (local $ptr i32)
    global.get $heap_ptr
    local.set $ptr
    global.get $heap_ptr
    local.get $size
    i32.add
    global.set $heap_ptr
    local.get $ptr)

  ;; build_ui() — Creates the entire UI by calling DTK host functions.
  ;; This function is the entry point: the DTK host calls it to build the UI.
  ;; Returns the main layout ID.
  (func (export "build_ui") (result i32)
    (local $main_layout i32)
    (local $hbox i32)
    (local $widget i32)

    ;; Set window title: "WASM-Powered Chat (DTK)"
    (call $dtk_set_window_title (i32.const 512) (i32.const 23))
    drop

    ;; Set window size: 600 x 480
    (call $dtk_set_window_size (i32.const 600) (i32.const 480))
    drop

    ;; Create main vertical layout
    (call $dtk_create_hbox (i32.const 0) (i32.const 0))
    local.set $main_layout

    ;; Create title label: "WASM-Powered Chat Client (DTK)"
    (call $dtk_create_label (i32.const 536) (i32.const 30))
    local.set $widget
    (call $dtk_add_to_layout (local.get $main_layout) (local.get $widget))
    drop

    ;; Create status label: "Initializing..."
    (call $dtk_create_label (i32.const 567) (i32.const 15))
    local.set $widget
    (call $dtk_add_to_layout (local.get $main_layout) (local.get $widget))
    drop

    ;; Create message display (readonly text edit)
    (call $dtk_create_textedit (i32.const 1) (i32.const 0))
    local.set $widget
    (call $dtk_add_to_layout (local.get $main_layout) (local.get $widget))
    drop

    ;; Create input row (horizontal box)
    (call $dtk_create_hbox (i32.const 1) (i32.const 0))
    local.set $hbox
    (call $dtk_add_to_layout (local.get $main_layout) (local.get $hbox))
    drop

    ;; Create input field
    (call $dtk_create_lineedit (i32.const 583) (i32.const 25))
    local.set $widget
    (call $dtk_add_to_layout (local.get $hbox) (local.get $widget))
    drop

    ;; Create send button
    (call $dtk_create_pushbutton (i32.const 609) (i32.const 4))
    local.set $widget
    (call $dtk_add_to_layout (local.get $hbox) (local.get $widget))
    drop

    ;; Return main layout ID
    local.get $main_layout)

  ;; format_and_display(ptr, len) — Prepends "[Me] " to the message,
  ;; then calls dtk_append_message to display it.
  ;; Returns 0 on success.
  (func (export "format_and_display") (param $ptr i32) (param $len i32) (result i32)
    (local $result_ptr i32)
    (local $i i32)

    ;; Allocate (len + 5) bytes for "[Me] " + message
    (call $alloc (i32.add (local.get $len) (i32.const 5)))
    local.set $result_ptr

    ;; Copy "[Me] " (5 bytes) from string offset 6
    i32.const 0
    local.set $i
    (block $prefix_break
      (loop $prefix_loop
        local.get $i
        i32.const 5
        i32.ge_s
        br_if $prefix_break
        ;; mem[result_ptr + i] = mem[stringOffsets[6].offset + i]
        (i32.store8
          (i32.add (local.get $result_ptr) (local.get $i))
          (i32.load8_u (i32.add (i32.const 643) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        br $prefix_loop))

    ;; Copy input message after the prefix
    i32.const 0
    local.set $i
    (block $msg_break
      (loop $msg_loop
        local.get $i
        local.get $len
        i32.ge_s
        br_if $msg_break
        ;; mem[result_ptr + 5 + i] = mem[ptr + i]
        (i32.store8
          (i32.add (i32.add (local.get $result_ptr) (i32.const 5)) (local.get $i))
          (i32.load8_u (i32.add (local.get $ptr) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        br $msg_loop))

    ;; Call DTK host to append the formatted message
    (call $dtk_append_message
      (local.get $result_ptr)
      (i32.add (local.get $len) (i32.const 5)))
    drop

    i32.const 0)
  ;; toggle_theme() — Gets current theme and toggles it via DTK host.
  (func (export "toggle_theme") (result i32)
    (local $current i32)
    (call $dtk_get_theme)
    local.set $current
    ;; If current is 0 (light), set to 1 (dark); else set to 0 (light)
    (if (i32.eqz (local.get $current))
      (then
        (call $dtk_set_theme (i32.const 1) (i32.const 0))
        drop)
      (else
        (call $dtk_set_theme (i32.const 0) (i32.const 0))
        drop))
    local.get $current))
