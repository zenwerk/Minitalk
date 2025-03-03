
/*
 * COMPILER.C	MiniTalk-to-bytecode compiler
 */

#include "minitalk.h"

/*------------------------------------*/
/* Bytecode Generator                 */
/*------------------------------------*/

int currentStacksize;
int maxStacksize;

void updateStack(int stackChange)
{
  currentStacksize += stackChange;
  if (currentStacksize < 0)
  {
    error("coder tried to code a pop from an empty stack");
  }
  if (currentStacksize > maxStacksize)
  {
    maxStacksize = currentStacksize;
  }
}

#define MAX_LITERAL_SIZE 256
Node *literalArray[MAX_LITERAL_SIZE];
Ushort literalSize;

Ushort makeLiteral(Node *literal)
{
  if (literalSize == MAX_LITERAL_SIZE)
  {
    error("too many literals in method");
  }
  literalArray[literalSize] = literal;
  return literalSize++;
}

#define MAX_CODE_SIZE 5000
Uchar codeArray[MAX_CODE_SIZE];
Ushort codeSize;

void codeByte(Uchar byte)
{
  if (codeSize == MAX_CODE_SIZE)
  {
    error("too many bytecodes in method");
  }
  codeArray[codeSize++] = byte;
}

Ushort getCurrentOffset(void)
{
  return codeSize;
}

void codeOffset(Ushort offset)
{
  codeByte(offset >> 8);
  codeByte(offset & 0xFF);
}

void patchOffset(Ushort address, Ushort offset)
{
  codeArray[address + 0] = offset >> 8;
  codeArray[address + 1] = offset & 0xFF;
}

void code0(Uchar bytecode, int stackChange)
{
  codeByte(bytecode);
  updateStack(stackChange);
}

void code1(Uchar bytecode, Uchar value, int stackChange)
{
  if (value < 16)
  {
    codeByte(bytecode | value);
  }
  else
  {
    codeByte(OP_EXTENDED | (bytecode >> 4));
    codeByte(value);
  }
  updateStack(stackChange);
}

void code2(Uchar bytecode, Uchar value, Uchar param, int stackChange)
{
  if (value < 16)
  {
    codeByte(bytecode | value);
  }
  else
  {
    codeByte(OP_EXTENDED | (bytecode >> 4));
    codeByte(value);
  }
  codeByte(param);
  updateStack(stackChange);
}

void code3(Uchar bytecode, Ushort offset, int stackChange)
{
  codeByte(bytecode);
  codeOffset(offset);
  updateStack(stackChange);
}

void codeLoad(Node *expression)
{
  Variable *variable;

  variable = expression->all.variable.record;
  switch (variable->type)
  {
    case V_SELF:
      code0(OP_PUSHSELF, 1);
      break;
    case V_SUPER:
      code0(OP_PUSHSELF, 1);
      break;
    case V_NIL:
      code0(OP_PUSHNIL, 1);
      break;
    case V_FALSE:
      code0(OP_PUSHFALSE, 1);
      break;
    case V_TRUE:
      code0(OP_PUSHTRUE, 1);
      break;
    case V_INSTANCE:
      code1(OP_PUSHINST, variable->offset, 1);
      break;
    case V_ARGUMENT:
      code1(OP_PUSHTEMP, variable->offset, 1);
      break;
    case V_TEMPORARY:
      code1(OP_PUSHTEMP, variable->offset, 1);
      break;
    case V_SHARED:
      code1(OP_PUSHASSOC, makeLiteral(expression), 1);
      break;
    default:
      error("codeLoad has illegal variable type");
      break;
  }
}

void codeStore(Node *expression)
{
  Variable *variable;

  variable = expression->all.variable.record;
  switch (variable->type)
  {
    case V_INSTANCE:
      code1(OP_STOREINST, variable->offset, -1);
      break;
    case V_TEMPORARY:
      code1(OP_STORETEMP, variable->offset, -1);
      break;
    case V_SHARED:
      code1(OP_STOREASSOC, makeLiteral(expression), -1);
      break;
    default:
      error("codeStore has illegal variable type");
      break;
  }
}

void codeExpression(Node *expression, Boolean valueNeeded)
{
  int numberArguments;
  Element *arguments;
  Element *messages;
  Element *variables;
  Ushort patchLocation;
  Element *statements;

  if (expression == NULL)
  {
    error("coder has empty expression");
  }
  switch (expression->type)
  {
    case N_SYMBOL:
    case N_INTNUM:
    case N_FLONUM:
    case N_STRING:
    case N_CHARCON:
    case N_ARRAY:
      if (valueNeeded)
      {
        code1(OP_PUSHLTRL, makeLiteral(expression), 1);
      }
      break;
    case N_VARIABLE:
      if (valueNeeded)
      {
        codeLoad(expression);
      }
      break;
    case N_BLOCK:
      if (valueNeeded)
      {
        code2(OP_PUSHBLOCK, expression->all.block.numberVariables, 10, 1);
        code3(OP_JUMP, 0, 0);
        patchLocation = getCurrentOffset() - 2;
        updateStack(expression->all.block.numberVariables);
        variables = expression->all.block.variables;
        while (variables != NULL)
        {
          codeStore(variables->element);
          variables = variables->next;
        }
        statements = expression->all.block.statements;
        if (statements == NULL)
        {
          code0(OP_PUSHNIL, 1);
          code0(OP_RETBLOCK, -1);
        }
        else
        {
          while (statements->next != NULL)
          {
            expression = statements->element;
            codeExpression(expression, FALSE);
            statements = statements->next;
          }
          expression = statements->element;
          if (expression->type == N_RETEXP)
          {
            codeExpression(expression->all.retexp.expression, TRUE);
            code0(OP_RET, -1);
          }
          else
          {
            codeExpression(expression, TRUE);
            code0(OP_RETBLOCK, -1);
          }
        }
        patchOffset(patchLocation, getCurrentOffset());
      }
      break;
    case N_MESSAGE:
      /* ATTENTION: if we have a null receiver (a cascaded message) */
      /* then do nothing since the receiver has already been pushed */
      if (expression->all.message.receiver != NULL)
      {
        codeExpression(expression->all.message.receiver, TRUE);
      }
      numberArguments = 0;
      arguments       = expression->all.message.arguments;
      while (arguments != NULL)
      {
        codeExpression(arguments->element, TRUE);
        numberArguments++;
        arguments = arguments->next;
      }
      if (!expression->all.message.superFlag)
      {
        code2(OP_SEND, numberArguments,
              makeLiteral(expression->all.message.selector), -numberArguments);
      }
      else
      {
        code2(OP_SENDSUPER, numberArguments,
              makeLiteral(expression->all.message.selector), -numberArguments);
      }
      if (!valueNeeded)
      {
        code0(OP_POP, -1);
      }
      break;
    case N_CASCADE:
      codeExpression(expression->all.cascade.receiver, TRUE);
      messages = expression->all.cascade.messages;
      while (messages->next != NULL)
      {
        code0(OP_DUP, 1);
        codeExpression(messages->element, FALSE);
        messages = messages->next;
      }
      codeExpression(messages->element, valueNeeded);
      break;
    case N_ASSIGN:
      codeExpression(expression->all.assign.expression, TRUE);
      variables = expression->all.assign.variables;
      while (variables->next != NULL)
      {
        code0(OP_DUP, 1);
        codeStore(variables->element);
        variables = variables->next;
      }
      if (valueNeeded)
      {
        code0(OP_DUP, 1);
      }
      codeStore(variables->element);
      break;
    case N_METHOD:
      /* If valueNeeded is true, then return the value of the last
         expression (instead of returning self). This is needed in
         case of interactively evaluating an expression. */
      statements = expression->all.method.statements;
      if (statements == NULL)
      {
        code0(OP_PUSHSELF, 1);
        code0(OP_RET, -1);
      }
      else
      {
        while (statements->next != NULL)
        {
          expression = statements->element;
          codeExpression(expression, FALSE);
          statements = statements->next;
        }
        expression = statements->element;
        if (expression->type == N_RETEXP)
        {
          codeExpression(expression->all.retexp.expression, TRUE);
          code0(OP_RET, -1);
        }
        else
        {
          if (valueNeeded)
          {
            codeExpression(expression, TRUE);
            code0(OP_RET, -1);
          }
          else
          {
            codeExpression(expression, FALSE);
            code0(OP_PUSHSELF, 1);
            code0(OP_RET, -1);
          }
        }
      }
      break;
    default:
      error("coder has illegal tree node");
      break;
  }
}

ObjPtr lookupGlobal(char *name)
{
  int length;
  ObjPtr dictionary;
  ObjPtr associations;
  ObjPtr association;
  ObjPtr key;

  length       = strlen(name);
  dictionary   = getPointer(machine.MiniTalk, VALUE_IN_ASSOCIATION);
  associations = getPointer(dictionary, ASSOCIATIONS_IN_DICTIONARY);
  while (associations != machine.nil)
  {
    association = getPointer(associations, OBJECT_IN_LINKEDOBJECT);
    key         = getPointer(association, KEY_IN_ASSOCIATION);
    if (strncmp(getBytes(key), name, length) == 0)
    {
      return association;
    }
    associations = getPointer(associations, NEXTLINK_IN_LINKEDOBJECT);
  }
  return machine.nil;
}

void codeLiteral(Node *literalNode)
{
  ObjPtr object;
  Element *elements;
  int i;
  char *name;

  switch (literalNode->type)
  {
    case N_SYMBOL:
      machine.compilerLiteral = newSymbol(literalNode->all.symbol.name);
      break;
    case N_INTNUM:
      machine.compilerLiteral = newSmallInteger(literalNode->all.intnum.value);
      break;
    case N_FLONUM:
      machine.compilerLiteral = newFloat(literalNode->all.flonum.value);
      break;
    case N_STRING:
      machine.compilerLiteral = newString(literalNode->all.string.value);
      break;
    case N_CHARCON:
      machine.compilerLiteral = newCharacter(literalNode->all.charcon.value);
      break;
    case N_ARRAY:
      /* Arrays must be constructed recursively. Use  */
      /* machine.compilerLiterals as temporary stack. */
      object =
        allocateObject(getPointer(machine.LinkedObject, VALUE_IN_ASSOCIATION),
                       SIZE_OF_LINKEDOBJECT, TRUE);
      setPointer(object, NEXTLINK_IN_LINKEDOBJECT, machine.compilerLiterals);
      machine.compilerLiterals = object;
      object = allocateObject(getPointer(machine.Array, VALUE_IN_ASSOCIATION),
                              literalNode->all.array.numberElements, TRUE);
      setPointer(machine.compilerLiterals, OBJECT_IN_LINKEDOBJECT, object);
      elements = literalNode->all.array.elements;
      i        = 0;
      while (elements != NULL)
      {
        codeLiteral(elements->element);
        setPointer(getPointer(machine.compilerLiterals, OBJECT_IN_LINKEDOBJECT),
                   i, machine.compilerLiteral);
        elements = elements->next;
        i++;
      }
      machine.compilerLiteral =
        getPointer(machine.compilerLiterals, OBJECT_IN_LINKEDOBJECT);
      machine.compilerLiterals =
        getPointer(machine.compilerLiterals, NEXTLINK_IN_LINKEDOBJECT);
      break;
    case N_VARIABLE:
      /* only shared variables appear here */
      name                    = literalNode->all.variable.record->name;
      machine.compilerLiteral = lookupGlobal(name);
      break;
    default:
      error("literal generator has illegal node type");
      break;
  }
}

void codeMethod(Node *method, Boolean lastValueNeeded)
{
  int i;
  ObjPtr selector;

  maxStacksize     = 0;
  currentStacksize = 0;
  codeSize         = 0;
  literalSize      = 0;
  codeExpression(method, lastValueNeeded);
  if (codeSize != 0)
  {
    machine.compilerCode = allocateObject(
      getPointer(machine.ByteArray, VALUE_IN_ASSOCIATION), codeSize, FALSE);
    memcpy(getBytes(machine.compilerCode), codeArray, codeSize);
  }
  else
  {
    machine.compilerCode = machine.nil;
  }
  if (literalSize != 0)
  {
    machine.compilerLiterals = allocateObject(
      getPointer(machine.Array, VALUE_IN_ASSOCIATION), literalSize, TRUE);
    for (i = 0; i < literalSize; i++)
    {
      codeLiteral(literalArray[i]);
      setPointer(machine.compilerLiterals, i, machine.compilerLiteral);
    }
  }
  else
  {
    machine.compilerLiterals = machine.nil;
  }
  machine.compilerMethod =
    allocateObject(getPointer(machine.CompiledMethod, VALUE_IN_ASSOCIATION),
                   SIZE_OF_COMPILEDMETHOD, TRUE);
  selector = newSymbol(method->all.method.selector->all.symbol.name);
  setPointer(machine.compilerMethod, SELECTOR_IN_COMPILEDMETHOD, selector);
  if (method->all.method.primitive != -1)
  {
    /* primitive call present */
    setPointer(machine.compilerMethod, PRIMITIVE_IN_COMPILEDMETHOD,
               newSmallInteger(method->all.method.primitive));
  }
  setPointer(machine.compilerMethod, NUMBERARGS_IN_COMPILEDMETHOD,
             newSmallInteger(method->all.method.numberArguments));
  setPointer(machine.compilerMethod, TEMPSIZE_IN_COMPILEDMETHOD,
             newSmallInteger(method->all.method.numberTemporaries));
  setPointer(machine.compilerMethod, STACKSIZE_IN_COMPILEDMETHOD,
             newSmallInteger(maxStacksize));
  setPointer(machine.compilerMethod, BYTECODES_IN_COMPILEDMETHOD,
             machine.compilerCode);
  setPointer(machine.compilerMethod, LITERALS_IN_COMPILEDMETHOD,
             machine.compilerLiterals);
  /* create association */
  machine.compilerAssociation =
    allocateObject(getPointer(machine.Association, VALUE_IN_ASSOCIATION),
                   SIZE_OF_ASSOCIATION, TRUE);
  selector = newSymbol(method->all.method.selector->all.symbol.name);
  setPointer(machine.compilerAssociation, KEY_IN_ASSOCIATION, selector);
  setPointer(machine.compilerAssociation, VALUE_IN_ASSOCIATION,
             machine.compilerMethod);
}

/*------------------------------------*/
/* Compiler Interface                 */
/*------------------------------------*/

void compile(char *aString, ObjPtr aClass, Boolean valueNeeded)
{
  Variable *variables;
  Node *method;

  initVariables(&variables, aClass);
  initScanner(aString);
  nextToken();
  method = parseMethod(&variables);
  if (tokenType != T_END)
  {
    parseError("additional characters after end of method");
  }
  computeOffsets(&variables, method);
  showVariables(&variables);
  showTree(method);
  machine.compilerClass = aClass;
  codeMethod(method, valueNeeded);
  freeTree(method);
  freeVariables(&variables);
}
