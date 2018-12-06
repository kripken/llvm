	.text
	.file	"irreducible-cfg.ll"
	.section	.text.test0,"",@
	.globl	test0
	.type	test0,@function
test0:
	.functype	test0 (i32, i32, i32, i32) -> ()
	block   	
	block   	
	br_if   	0, $2
	i32.const	$2=, 0
	i32.const	$5=, 1
	br      	1
.LBB0_3:
	end_block
	i32.const	$push0=, 3
	i32.shl 	$push1=, $3, $pop0
	i32.add 	$push2=, $0, $pop1
	f64.load	$4=, 0($pop2):p2align=2
	i32.const	$5=, 0
.LBB0_5:
	end_block
	loop    	
	block   	
	block   	
	block   	
	block   	
	br_table 	$5, 1, 0, 0
.LBB0_6:
	end_block
	i32.ge_s	$push3=, $2, $1
	br_if   	1, $pop3
	i32.const	$push4=, 3
	i32.shl 	$push5=, $2, $pop4
	i32.add 	$push17=, $0, $pop5
	tee_local	$push16=, $3=, $pop17
	f64.load	$push6=, 0($3):p2align=2
	f64.const	$push7=, 0x1.2666666666666p1
	f64.mul 	$push15=, $pop6, $pop7
	tee_local	$push14=, $4=, $pop15
	f64.store	0($pop16):p2align=2, $pop14
	i32.const	$5=, 0
	br      	3
.LBB0_9:
	end_block
	i32.const	$push10=, 3
	i32.shl 	$push11=, $2, $pop10
	i32.add 	$push12=, $0, $pop11
	f64.const	$push8=, 0x1.4cccccccccccdp0
	f64.add 	$push9=, $4, $pop8
	f64.store	0($pop12):p2align=2, $pop9
	i32.const	$push13=, 1
	i32.add 	$2=, $2, $pop13
	br      	1
.LBB0_10:
	end_block
	return
.LBB0_11:
	end_block
	i32.const	$5=, 1
	br      	0
.LBB0_12:
	end_loop
	end_function
.Lfunc_end0:
	.size	test0, .Lfunc_end0-test0

	.section	.text.test1,"",@
	.globl	test1
	.type	test1,@function
test1:
	.functype	test1 (i32, i32, i32, i32) -> ()
	block   	
	block   	
	br_if   	0, $2
	i32.const	$3=, 0
	i32.const	$5=, 1
	br      	1
.LBB1_3:
	end_block
	i32.const	$push0=, 3
	i32.shl 	$push1=, $3, $pop0
	i32.add 	$push2=, $0, $pop1
	f64.load	$4=, 0($pop2):p2align=2
	i32.const	$5=, 0
.LBB1_5:
	end_block
	loop    	
	block   	
	block   	
	block   	
	block   	
	br_table 	$5, 1, 0, 0
.LBB1_6:
	end_block
	i32.ge_s	$push3=, $3, $1
	br_if   	1, $pop3
	i32.const	$push4=, 3
	i32.shl 	$push5=, $3, $pop4
	i32.add 	$push18=, $0, $pop5
	tee_local	$push17=, $2=, $pop18
	f64.load	$push6=, 0($2):p2align=2
	f64.const	$push7=, 0x1.2666666666666p1
	f64.mul 	$push16=, $pop6, $pop7
	tee_local	$push15=, $4=, $pop16
	f64.store	0($pop17):p2align=2, $pop15
	i32.const	$2=, 0
.LBB1_8:
	loop    	
	i32.const	$push22=, 1
	i32.add 	$push21=, $2, $pop22
	tee_local	$push20=, $2=, $pop21
	i32.const	$push19=, 256
	i32.lt_s	$push8=, $pop20, $pop19
	br_if   	0, $pop8
	end_loop
	i32.const	$5=, 0
	br      	3
.LBB1_10:
	end_block
	i32.const	$push11=, 3
	i32.shl 	$push12=, $3, $pop11
	i32.add 	$push13=, $0, $pop12
	f64.const	$push9=, 0x1.4cccccccccccdp0
	f64.add 	$push10=, $4, $pop9
	f64.store	0($pop13):p2align=2, $pop10
	i32.const	$push14=, 1
	i32.add 	$3=, $3, $pop14
	br      	1
.LBB1_11:
	end_block
	return
.LBB1_12:
	end_block
	i32.const	$5=, 1
	br      	0
.LBB1_13:
	end_loop
	end_function
.Lfunc_end1:
	.size	test1, .Lfunc_end1-test1

	.section	.text.test2,"",@
	.type	test2,@function
test2:
	.functype	test2 (i32) -> (i32)
	block   	
	block   	
	i32.const	$push0=, 1
	i32.call	$push1=, test2@FUNCTION, $pop0
	br_if   	0, $pop1
	i32.const	$1=, 0
	br      	1
.LBB2_2:
	end_block
	i32.const	$1=, 1
.LBB2_3:
	end_block
	loop    	i32
	block   	
	block   	
	br_table 	$1, 0, 1, 1
.LBB2_4:
	end_block
.LBB2_5:
	loop    	
	i32.const	$push4=, 2
	i32.call	$push2=, test2@FUNCTION, $pop4
	i32.eqz 	$push6=, $pop2
	br_if   	0, $pop6
	end_loop
	i32.const	$1=, 1
	br      	1
.LBB2_7:
	end_block
.LBB2_8:
	loop    	
	i32.const	$push5=, 3
	i32.call	$push3=, test2@FUNCTION, $pop5
	br_if   	0, $pop3
	end_loop
	i32.const	$1=, 0
	br      	0
.LBB2_10:
	end_loop
	end_function
.Lfunc_end2:
	.size	test2, .Lfunc_end2-test2


