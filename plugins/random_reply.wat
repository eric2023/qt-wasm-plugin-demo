;;;; Backend WASM Plugin: random_reply.wat
;;; Generated from reply table - do not edit manually.
;;; Given a seed (seed % 10), selects one of 10 canned replies.

(module
  (memory (export "memory") 1)

  (global $heap_ptr (mut i32) (i32.const 1024))
  (global $reply_len (mut i32) (i32.const 0))

  (func $alloc (export "alloc") (param $size i32) (result i32)
    (local $ptr i32)
    global.get $heap_ptr
    local.set $ptr
    global.get $heap_ptr
    local.get $size
    i32.add
    global.set $heap_ptr
    local.get $ptr)

  (func (export "get_heap_ptr") (result i32)
    global.get $heap_ptr)

  (func (export "get_reply_len") (result i32)
    global.get $reply_len)

  (func $write_byte (param $addr i32) (param $val i32)
    local.get $addr
    local.get $val
    i32.store8)

  ;; Reply 0: "Hello! Nice to meet you." (24 bytes)
  (func $write_reply_0 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 72)) ;; H
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 108)) ;; l
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 108)) ;; l
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 33)) ;; !
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 78)) ;; N
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 105)) ;; i
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 99)) ;; c
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 109)) ;; m
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 19)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 20)) (i32.const 121)) ;; y
    (call $write_byte (i32.add (local.get $base) (i32.const 21)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 22)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 23)) (i32.const 46)) ;; .
  )

  ;; Reply 1: "I am a WASM-powered bot." (24 bytes)
  (func $write_reply_1 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 73)) ;; I
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 109)) ;; m
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 87)) ;; W
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 65)) ;; A
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 83)) ;; S
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 77)) ;; M
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 45)) ;; -
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 112)) ;; p
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 119)) ;; w
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 114)) ;; r
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 100)) ;; d
    (call $write_byte (i32.add (local.get $base) (i32.const 19)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 20)) (i32.const 98)) ;; b
    (call $write_byte (i32.add (local.get $base) (i32.const 21)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 22)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 23)) (i32.const 46)) ;; .
  )

  ;; Reply 2: "Tell me more about it." (22 bytes)
  (func $write_reply_2 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 84)) ;; T
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 108)) ;; l
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 108)) ;; l
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 109)) ;; m
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 109)) ;; m
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 114)) ;; r
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 98)) ;; b
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 19)) (i32.const 105)) ;; i
    (call $write_byte (i32.add (local.get $base) (i32.const 20)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 21)) (i32.const 46)) ;; .
  )

  ;; Reply 3: "WASM plugins are awesome!" (25 bytes)
  (func $write_reply_3 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 87)) ;; W
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 65)) ;; A
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 83)) ;; S
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 77)) ;; M
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 112)) ;; p
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 108)) ;; l
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 103)) ;; g
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 105)) ;; i
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 110)) ;; n
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 114)) ;; r
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 119)) ;; w
    (call $write_byte (i32.add (local.get $base) (i32.const 19)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 20)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 21)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 22)) (i32.const 109)) ;; m
    (call $write_byte (i32.add (local.get $base) (i32.const 23)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 24)) (i32.const 33)) ;; !
  )

  ;; Reply 4: "Interesting question!" (21 bytes)
  (func $write_reply_4 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 73)) ;; I
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 110)) ;; n
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 114)) ;; r
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 105)) ;; i
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 110)) ;; n
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 103)) ;; g
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 113)) ;; q
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 105)) ;; i
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 19)) (i32.const 110)) ;; n
    (call $write_byte (i32.add (local.get $base) (i32.const 20)) (i32.const 33)) ;; !
  )

  ;; Reply 5: "Let me think about that..." (26 bytes)
  (func $write_reply_5 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 76)) ;; L
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 109)) ;; m
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 104)) ;; h
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 105)) ;; i
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 110)) ;; n
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 107)) ;; k
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 98)) ;; b
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 19)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 20)) (i32.const 104)) ;; h
    (call $write_byte (i32.add (local.get $base) (i32.const 21)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 22)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 23)) (i32.const 46)) ;; .
    (call $write_byte (i32.add (local.get $base) (i32.const 24)) (i32.const 46)) ;; .
    (call $write_byte (i32.add (local.get $base) (i32.const 25)) (i32.const 46)) ;; .
  )

  ;; Reply 6: "That sounds great!" (18 bytes)
  (func $write_reply_6 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 84)) ;; T
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 104)) ;; h
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 110)) ;; n
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 100)) ;; d
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 103)) ;; g
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 114)) ;; r
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 33)) ;; !
  )

  ;; Reply 7: "I see what you mean." (20 bytes)
  (func $write_reply_7 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 73)) ;; I
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 119)) ;; w
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 104)) ;; h
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 121)) ;; y
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 109)) ;; m
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 110)) ;; n
    (call $write_byte (i32.add (local.get $base) (i32.const 19)) (i32.const 46)) ;; .
  )

  ;; Reply 8: "Could you rephrase that?" (24 bytes)
  (func $write_reply_8 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 67)) ;; C
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 108)) ;; l
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 100)) ;; d
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 121)) ;; y
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 111)) ;; o
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 114)) ;; r
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 112)) ;; p
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 104)) ;; h
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 114)) ;; r
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 19)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 20)) (i32.const 104)) ;; h
    (call $write_byte (i32.add (local.get $base) (i32.const 21)) (i32.const 97)) ;; a
    (call $write_byte (i32.add (local.get $base) (i32.const 22)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 23)) (i32.const 63)) ;; ?
  )

  ;; Reply 9: "WASM is the future!" (19 bytes)
  (func $write_reply_9 (param $base i32)
    (call $write_byte (local.get $base) (i32.const 87)) ;; W
    (call $write_byte (i32.add (local.get $base) (i32.const 1)) (i32.const 65)) ;; A
    (call $write_byte (i32.add (local.get $base) (i32.const 2)) (i32.const 83)) ;; S
    (call $write_byte (i32.add (local.get $base) (i32.const 3)) (i32.const 77)) ;; M
    (call $write_byte (i32.add (local.get $base) (i32.const 4)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 5)) (i32.const 105)) ;; i
    (call $write_byte (i32.add (local.get $base) (i32.const 6)) (i32.const 115)) ;; s
    (call $write_byte (i32.add (local.get $base) (i32.const 7)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 8)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 9)) (i32.const 104)) ;; h
    (call $write_byte (i32.add (local.get $base) (i32.const 10)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 11)) (i32.const 32)) ;;   
    (call $write_byte (i32.add (local.get $base) (i32.const 12)) (i32.const 102)) ;; f
    (call $write_byte (i32.add (local.get $base) (i32.const 13)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 14)) (i32.const 116)) ;; t
    (call $write_byte (i32.add (local.get $base) (i32.const 15)) (i32.const 117)) ;; u
    (call $write_byte (i32.add (local.get $base) (i32.const 16)) (i32.const 114)) ;; r
    (call $write_byte (i32.add (local.get $base) (i32.const 17)) (i32.const 101)) ;; e
    (call $write_byte (i32.add (local.get $base) (i32.const 18)) (i32.const 33)) ;; !
  )

  (func (export "generate_reply") (param $seed i32) (result i32)
    (local $index i32)
    (local $result_ptr i32)

    ;; index = |seed| % 10
    local.get $seed
    i32.const 0
    i32.lt_s
    if
      i32.const 0
      local.get $seed
      i32.sub
      i32.const 10
      i32.rem_u
      local.set $index
    else
      local.get $seed
      i32.const 10
      i32.rem_u
      local.set $index
    end

    ;; Allocate 32 bytes
    (call $alloc (i32.const 32))
    local.set $result_ptr

    ;; Dispatch
    local.get $index
    i32.const 0
    i32.eq
    if
      local.get $result_ptr
      call $write_reply_0
      i32.const 24
      global.set $reply_len
    else
      local.get $index
      i32.const 1
      i32.eq
      if
        local.get $result_ptr
        call $write_reply_1
        i32.const 24
        global.set $reply_len
      else
        local.get $index
        i32.const 2
        i32.eq
        if
          local.get $result_ptr
          call $write_reply_2
          i32.const 22
          global.set $reply_len
        else
          local.get $index
          i32.const 3
          i32.eq
          if
            local.get $result_ptr
            call $write_reply_3
            i32.const 25
            global.set $reply_len
          else
            local.get $index
            i32.const 4
            i32.eq
            if
              local.get $result_ptr
              call $write_reply_4
              i32.const 21
              global.set $reply_len
            else
              local.get $index
              i32.const 5
              i32.eq
              if
                local.get $result_ptr
                call $write_reply_5
                i32.const 26
                global.set $reply_len
              else
                local.get $index
                i32.const 6
                i32.eq
                if
                  local.get $result_ptr
                  call $write_reply_6
                  i32.const 18
                  global.set $reply_len
                else
                  local.get $index
                  i32.const 7
                  i32.eq
                  if
                    local.get $result_ptr
                    call $write_reply_7
                    i32.const 20
                    global.set $reply_len
                  else
                    local.get $index
                    i32.const 8
                    i32.eq
                    if
                      local.get $result_ptr
                      call $write_reply_8
                      i32.const 24
                      global.set $reply_len
                    else
                      local.get $index
                      i32.const 9
                      i32.eq
                      if
                        local.get $result_ptr
                        call $write_reply_9
                        i32.const 19
                        global.set $reply_len
                      end
                    end
                  end
                end
              end
            end
          end
        end
      end
    end

    local.get $result_ptr))
