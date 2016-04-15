define void @foo8(i32 %x) #0 {
entry:
  %cmp = icmp ne i32 %x, 10
  br i1 %cmp, label %if.end3, label %cont

cont:
  %z = add i32 %x, 1
  %cmp1 = icmp ne i32 %x, 20
  br i1 %cmp1, label %if.end3, label %cont2

cont2: ;; LVI should be able to tell we can't get here!
  %y = add i32 %x, 1
  br label %if.end3

if.end3:
  ret void
}
