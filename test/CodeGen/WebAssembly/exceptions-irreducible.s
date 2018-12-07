	.text
	.file	"exceptions-irreducible.ll"
	.section	.text.crashy,"",@
	.hidden	crashy                  # -- Begin function crashy
	.globl	crashy
	.type	crashy,@function
crashy:                                 # @crashy
	.functype	crashy () -> ()
	.local  	i32, i32
# %bb.0:                                # %entry
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	call	__invoke_void@FUNCTION
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	block   	
	block   	
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	0               # 0: down to label1
# %bb.1:                                # %invoke.cont
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	call	__invoke_void@FUNCTION
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	1               # 1: down to label0
# %bb.2:                                # %invoke.cont4
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.call	"__invoke_i8*"@FUNCTION
	drop
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	1               # 1: down to label0
# %bb.3:                                # %invoke.cont6
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	call	__invoke_void@FUNCTION
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	block   	
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	0               # 0: down to label2
# %bb.4:                                # %invoke.cont13
.LBB0_5:                                # %for.cond
                                        # =>This Loop Header: Depth=1
                                        #     Child Loop BB0_8 Depth 2
	loop    	                # label3:
	block   	
	block   	
	i32.const	0
	br_if   	0               # 0: down to label5
# %bb.6:                                #   in Loop: Header=BB0_5 Depth=1
	i32.const	0
	set_local	1
	br      	1               # 1: down to label4
.LBB0_7:                                #   in Loop: Header=BB0_5 Depth=1
	end_block                       # label5:
	i32.const	1
	set_local	1
.LBB0_8:                                #   Parent Loop BB0_5 Depth=1
                                        # =>  This Inner Loop Header: Depth=2
	end_block                       # label4:
	loop    	                # label6:
	block   	
	block   	
	block   	
	block   	
	block   	
	block   	
	get_local	1
	br_table 	0, 1, 1         # 0: down to label12
                                        # 1: down to label11
.LBB0_9:                                # %land.lhs.true.i.i.i
                                        #   in Loop: Header=BB0_8 Depth=2
	end_block                       # label12:
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.call	__invoke_i32@FUNCTION
	drop
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	1               # 1: down to label10
# %bb.10:                               #   in Loop: Header=BB0_8 Depth=2
	i32.const	1
	set_local	1
	br      	5               # 5: up to label6
.LBB0_11:                               # %_ZNKSt3__219istreambuf_iteratorIcNS_11char_traitsIcEEE14__test_for_eofEv.exit.i.i
                                        #   in Loop: Header=BB0_8 Depth=2
	end_block                       # label11:
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.call	__invoke_i32@FUNCTION
	drop
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	0               # 0: down to label10
# %bb.12:                               # %_ZNSt3__215basic_streambufIcNS_11char_traitsIcEEE5sgetcEv.exit.i19.i.i
                                        #   in Loop: Header=BB0_8 Depth=2
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	call	__invoke_void@FUNCTION
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	1               # 1: down to label9
# %bb.13:                               # %invoke.cont23
                                        #   in Loop: Header=BB0_8 Depth=2
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	call	__invoke_void@FUNCTION
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	1               # 1: down to label9
# %bb.14:                               # %invoke.cont25
                                        #   in Loop: Header=BB0_8 Depth=2
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.call	__invoke_i32@FUNCTION
	drop
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	0               # 0: down to label10
# %bb.15:                               # %invoke.cont29
                                        #   in Loop: Header=BB0_8 Depth=2
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	call	__invoke_void@FUNCTION
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.eq  
	br_if   	0               # 0: down to label10
# %bb.16:                               # %invoke.cont33
                                        #   in Loop: Header=BB0_8 Depth=2
	block   	
	i32.const	0
	i32.eqz
	br_if   	0               # 0: down to label13
# %bb.17:                               # %if.end.i.i146
                                        #   in Loop: Header=BB0_8 Depth=2
	get_local	0
	call_indirect	.Ltypeindex0@TYPEINDEX
	i32.const	0
	i32.eqz
	br_if   	4               # 4: down to label7
	br      	3               # 3: down to label8
.LBB0_18:                               # %if.then.i.i144
                                        #   in Loop: Header=BB0_5 Depth=1
	end_block                       # label13:
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.call	__invoke_i32@FUNCTION
	drop
	i32.const	0
	i32.load	__THREW__
	set_local	0
	i32.const	0
	i32.const	0
	i32.store	__THREW__
	get_local	0
	i32.const	1
	i32.ne  
	br_if   	5               # 5: up to label3
.LBB0_19:                               # %lpad16.loopexit
	end_block                       # label10:
	i32.call	__cxa_find_matching_catch_2@FUNCTION
	drop
	i32.call	getTempRet0@FUNCTION
	drop
	unreachable
.LBB0_20:                               # %lpad22
	end_block                       # label9:
	i32.call	__cxa_find_matching_catch_2@FUNCTION
	drop
	i32.call	getTempRet0@FUNCTION
	drop
	unreachable
.LBB0_21:                               #   in Loop: Header=BB0_8 Depth=2
	end_block                       # label8:
	i32.const	1
	set_local	1
	br      	1               # 1: up to label6
.LBB0_22:                               #   in Loop: Header=BB0_8 Depth=2
	end_block                       # label7:
	i32.const	0
	set_local	1
	br      	0               # 0: up to label6
.LBB0_23:                               # %lpad12
	end_loop
	end_loop
	end_block                       # label2:
	i32.call	__cxa_find_matching_catch_2@FUNCTION
	drop
	i32.call	getTempRet0@FUNCTION
	drop
	get_local	0
	call	__resumeException@FUNCTION
	unreachable
.LBB0_24:                               # %lpad
	end_block                       # label1:
	i32.call	__cxa_find_matching_catch_2@FUNCTION
	drop
	i32.call	getTempRet0@FUNCTION
	drop
	unreachable
.LBB0_25:                               # %lpad3
	end_block                       # label0:
	i32.call	__cxa_find_matching_catch_2@FUNCTION
	drop
	i32.call	getTempRet0@FUNCTION
	drop
	unreachable
	end_function
.Lfunc_end0:
	.size	crashy, .Lfunc_end0-crashy
                                        # -- End function

	.ident	"clang version 8.0.0 (git@github.com:llvm-mirror/clang.git f8ec7c38feebd5cccae31acc7a50182b5474bfa9) (git@github.com:llvm-mirror/llvm.git eb54373b783a878c7397bc4503e93563c322e19a)"
	.functype	__gxx_personality_v0 (i32) -> (i32)
	.functype	getTempRet0 () -> (i32)
	.functype	setTempRet0 (i32) -> ()
	.functype	__resumeException (i32) -> ()
	.functype	llvm_eh_typeid_for (i32) -> (i32)
	.functype	__invoke_void (i32) -> ()
	.functype	__invoke_i8* (i32) -> (i32)
	.functype	__invoke_i32 (i32) -> (i32)
	.functype	__cxa_find_matching_catch_2 () -> (i32)
	.size	__THREW__, 4
	.size	__threwValue, 4
