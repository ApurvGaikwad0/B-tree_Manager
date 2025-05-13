# Makefile for the assignment
# This Makefile is used to compile the test files and the source files for the assignment
# It will create two executables: test_assign4 and test_expr
.PHONY: all
all: test_expr test_assign4

test_assign4: test_assign4_1.c btree_mgr.c record_mgr.c rm_serializer.c expr.c buffer_mgr_stat.c storage_mgr.c dberror.c buffer_mgr.c 
	gcc -o test_assign4 test_assign4_1.c btree_mgr.c record_mgr.c rm_serializer.c expr.c buffer_mgr_stat.c storage_mgr.c dberror.c buffer_mgr.c

test_expr: test_expr.c btree_mgr.c record_mgr.c rm_serializer.c expr.c buffer_mgr_stat.c storage_mgr.c dberror.c buffer_mgr.c
	gcc -o test_expr test_expr.c btree_mgr.c record_mgr.c rm_serializer.c expr.c buffer_mgr_stat.c storage_mgr.c dberror.c buffer_mgr.c




.PHONY: clean
clean:
	rm test_assign4 test_expr
