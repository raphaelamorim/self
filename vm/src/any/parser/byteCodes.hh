/* Sun-$Revision: 30.11 $ */

/* Copyright 1992-2006 Sun Microsystems, Inc. and Stanford University.
   See the LICENSE file for license information. */

# ifdef INTERFACE_PRAGMAS
  # pragma interface
# endif


# define OPWIDTH        4
# define INDEXWIDTH     (8 - OPWIDTH)

# define MAXOP          nthMask(OPWIDTH)
# define MAXINDEX       nthMask(INDEXWIDTH)

enum ByteCodeKind {
  INDEX_CODE,           // shift index left, OR in my index field
  LITERAL_CODE,         // push lits[index]
  SEND_CODE,            // send w/ receiver on stack,
                        // selector in literals[indx]
  IMPLICIT_SEND_CODE,   // send w/ implicit receiver,
                        // selector in literals[indx]
  
  NO_OPERAND_CODE,      // no operand, opcode in index bits, see NoOparandKind
  READ_LOCAL_CODE,      // read  local; lexical lvl in lex reg, see below
  WRITE_LOCAL_CODE,     // write local; lexical lvl in lex reg, see below
  LEXICAL_LEVEL_CODE,   // lexical level of next RW_LOCAL op
  
  BRANCH_CODE,          // uncond branch via lit
  BRANCH_TRUE_CODE,     // branch on true via literal
  BRANCH_FALSE_CODE,    // branch on false via literal
  BRANCH_INDEXED_CODE,  // branch via literal vector, index on stack
  
  DELEGATEE_CODE,       // delegate next send to lits[index]
  
  ARGUMENT_COUNT_CODE,  // optional bc. for Klein: gives arg. count of next send
  
  // For legacy snapshots, the method uses the old codes (through
  // DELGATEE_CODE) unless it starts with INSTRUCTION_SET_SELECTION_CODE
  // below. -- dmu 10/01
  INSTRUCTION_SET_SELECTION_CODE = ARGUMENT_COUNT_CODE
};

enum NoOperandKind {
  SELF_CODE,              // push self onto stack
  POP_CODE,               // pop value from stack
  NONLOCAL_RETURN_CODE,   // non-local-return from block
  UNDIRECTED_RESEND_CODE  // next send is an undirected resend
};

enum InstructionSetKind {
  TWENTIETH_CENTURY_INSTRUCTION_SET = -1, // don't spec = this one
  TWENTIETH_CENTURY_PLUS_ARGUMENT_COUNT_INSTRUCTION_SET,
  LAST_INSTRUCTION_SET = TWENTIETH_CENTURY_PLUS_ARGUMENT_COUNT_INSTRUCTION_SET
};

inline ByteCodeKind getOp(u_char c) { return ByteCodeKind(c >> INDEXWIDTH); }
inline fint getIndex(fint c)        { return c & MAXINDEX; }
inline NoOperandKind getNoOpOp(u_char c) {return NoOperandKind(getIndex(c));}

inline bool isSendOp(ByteCodeKind op)   {
  return op == SEND_CODE || op == IMPLICIT_SEND_CODE;
}

inline fint BuildCode(fint op, fint x) {
  assert(x <= MAXINDEX, "bad index");
  return (op << INDEXWIDTH) | x; 
}
  
    
// the position table is generated by a parallel process, so
//  have one abstract superclass:

class AbstractByteCode: public preservedVmObj {
 public:
  
  objVectorOop literals;
  fint literalIndex;
  fint maxLiteralIndex;
  int32 stack_depth; // for branch checking
  
  bool mustAllocate;

   LabelSet*  labelSet;
  BranchSet* branchSet;
  
  char* errorMessage;
  bool  ranOutOfMemory;
  
 public:
 
   AbstractByteCode( bool ma, objVectorOop lits = NULL) {
     mustAllocate = ma;
     
     if ( lits == NULL ) {
       literals= Memory->literalsObj->cloneSize(50);
       literalIndex= 0;
     }
     else {
       literals = lits;
       literalIndex= lits->length();
     }
     maxLiteralIndex= literals->length();

     stack_depth= 0;
     labelSet=  new LabelSet;
     branchSet= new BranchSet;
     errorMessage = "";
     ranOutOfMemory = false;
   }
   
  // generation routines
  
  void GenLiteralByteCode(   fint offset, fint length, oop literal);
  void GenDelegateeByteCode( fint offset, fint length, stringOop delegatee);

  void GenSendByteCode( fint offset, fint length,
                        stringOop selector, 
                        bool isSelfImplicit,
                        bool isUndirectedResend,
                        stringOop resendTarget);

  void GenSelfByteCode(fint offset, fint length);
  void GenPopByteCode( fint offset, fint length);
  void GenUndirectedResendByteCode( fint offset, fint length);
  void GenNonLocalReturnByteCode(fint offset, fint length);
  void GenRWLocalByteCode( fint offset, fint length,
                           bool isRead,
                           int32 lexicalLevel,
                           int32 index);
                           
  bool GenBranchByteCode(        fint offset, fint length, oop label);
  bool GenBranchTrueByteCode(    fint offset, fint length, oop label);
  bool GenBranchFalseByteCode(   fint offset, fint length, oop label);
  bool GenBranchIndexedByteCode( fint offset, fint length, objVectorOop labels);
  
  void GenInstructionSetSelectionByteCode(fint offset, fint length, InstructionSetKind);
  
  bool GenLabelDefinition( oop label);
  
  bool ResolveBranches();
                           

  // preserve  operation
  void oops_do(oopsDoFn f) { 
    (*f)((oop*)&literals);
    branchSet->oops_do(f);
    labelSet->oops_do(f);
  }
  // helpers
  virtual bool isPositionTable() = 0;
  virtual void GenCode(fint offset, fint length, fint c) = 0;
  virtual fint bci() = 0;

  bool GenSimpleBranchByteCode( fint offset,
                                fint length,
                                oop label,
                                ByteCodeKind op );
  fint GenLiteral(oop p);
  // must make a new object so literal slot won't get reused
  fint GenLabelLiteral() { return GenLiteral( Memory->nilObj->clone()); }
  fint GenIndex(fint offset, fint length, fint x);
  fint GenExtendedIndex(fint offset, fint length, fint x);

  int32 getLabelIndex( oop label);
};

class ByteCode: public AbstractByteCode {
 public:
  
  // instance variables
  byteVectorOop codes; // the finished vector is a canonical string
  fint codeIndex;
  fint maxCodeIndex;
  
  stringOop file;
  smiOop    line;
  stringOop source;
  smiOop    sourceOffset;
  smiOop    sourceLen;

  // constructors
       
  ByteCode(bool ma) 
  : AbstractByteCode(ma) {
    maxCodeIndex = 100;
    codeIndex = 0;
    codes= Memory->byteVectorObj->cloneSize(maxCodeIndex);
    file = NULL; source = NULL;     // must initialize for GC
    sourceOffset= smiOop_zero;
    sourceLen=    smiOop_zero;
    
    if (GenArgCountBytecode) {
      // Use new Iset from now now -- dmu 10/01
      GenInstructionSetSelectionByteCode(0, 0, TWENTIETH_CENTURY_PLUS_ARGUMENT_COUNT_INSTRUCTION_SET);
    }
  }

  
  ByteCode(bool ma, byteVectorOop c,  objVectorOop l, stringOop f, smiOop ln, 
           stringOop s) 
        : AbstractByteCode( ma, l ) {
    codes = c;
    codeIndex = maxCodeIndex = c->length();

    file = f;
    line = ln;
    source = s;
    sourceOffset= smiOop_zero;
    sourceLen=    smiOop_zero;
  }


  bool Finish();
  bool Finish(char* fname, fint sourceLine, char* srcStart, fint srcLen);
  bool Finish(char* fname, char* src);
  bool Finish(char* fname, fint sourceLine, fint srcOffset, fint srcLen);
  
  // preserve  operation
  void oops_do(oopsDoFn f) { 
    AbstractByteCode::oops_do(f);
    (*f)((oop*)&codes);
    (*f)((oop*)&file);
    (*f)((oop*)&source);
  }


  // helpers

  bool isPositionTable() { return false; }
  
  void GenCode(fint offset, fint length, fint c);
  
  fint bci() { return codeIndex; }
  

  // programming

  friend oop create_outer_method_prim( oop ignore,
                                       byteVectorOop bv,
                                       objVectorOop lits,
                                       stringOop file,
                                       smiOop line,
                                       stringOop source);
  
  friend oop create_block_method_prim( oop ignore,
                                      byteVectorOop bv,
                                      objVectorOop lits,
                                      stringOop file,
                                      smiOop line,
                                      stringOop source);
};
