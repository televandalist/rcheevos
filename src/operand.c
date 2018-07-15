#include "internal.h"

#include <stdlib.h>
#include <ctype.h>

#include <lua.h>
#include <lauxlib.h>

static int rc_parse_operand_lua(rc_operand_t* self, const char** memaddr, lua_State* L, int funcs_ndx) {
  const char* aux = *memaddr;
  const char* id;

  if (*aux++ != '@') {
    return RC_INVALID_LUA_OPERAND;
  }

  if (!isalpha(*aux)) {
    return RC_INVALID_LUA_OPERAND;
  }

  id = aux;

  while (isalnum(*aux) || *aux == '_') {
    aux++;
  }

  if (L != 0) {
    if (!lua_istable(L, funcs_ndx)) {
      return RC_INVALID_LUA_OPERAND;
    }

    lua_pushlstring(L, id, aux - id);
    lua_gettable(L, funcs_ndx);

    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      return RC_INVALID_LUA_OPERAND;
    }

    self->function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
  }

  self->type = RC_OPERAND_LUA;
  *memaddr = aux;
  return RC_OK;
}

static int rc_parse_operand_memory(rc_operand_t* self, const char** memaddr) {
  const char* aux = *memaddr;
  char* end;

  switch (*aux++) {
    case 'd': case 'D':
      self->type = RC_OPERAND_DELTA;
      break;

    case 'b': case 'B':
      self->type = RC_OPERAND_ADDRESS;
      self->is_bcd = 1;
      break;

    default:
      self->type = RC_OPERAND_ADDRESS;
      aux--;
      break;
  }

  if (*aux++ != '0') {
    return RC_INVALID_MEMORY_OPERAND;
  }

  if (*aux != 'x' && *aux != 'X') {
    return RC_INVALID_MEMORY_OPERAND;
  }

  aux++;

  switch (*aux++) {
    case 'm': case 'M': self->size = RC_OPERAND_BIT_0; break;
    case 'n': case 'N': self->size = RC_OPERAND_BIT_1; break;
    case 'o': case 'O': self->size = RC_OPERAND_BIT_2; break;
    case 'p': case 'P': self->size = RC_OPERAND_BIT_3; break;
    case 'q': case 'Q': self->size = RC_OPERAND_BIT_4; break;
    case 'r': case 'R': self->size = RC_OPERAND_BIT_5; break;
    case 's': case 'S': self->size = RC_OPERAND_BIT_6; break;
    case 't': case 'T': self->size = RC_OPERAND_BIT_7; break;
    case 'l': case 'L': self->size = RC_OPERAND_LOW; break;
    case 'u': case 'U': self->size = RC_OPERAND_HIGH; break;
    case 'h': case 'H': self->size = RC_OPERAND_8_BITS; break;
    case ' ':           self->size = RC_OPERAND_16_BITS; break;
    case 'x': case 'X': self->size = RC_OPERAND_32_BITS; break;

    default:
      self->size = RC_OPERAND_16_BITS;
      aux--;
      break;
  }

  self->value = (unsigned)strtoul(aux, &end, 16);

  if (end == aux) {
    return RC_INVALID_MEMORY_OPERAND;
  }

  *memaddr = end;
  return RC_OK;
}

static int rc_parse_operand_trigger(rc_operand_t* self, const char** memaddr, lua_State* L, int funcs_ndx) {
  const char* aux = *memaddr;
  char* end;
  int ret;

  switch (*aux) {
    case 'h': case 'H':
      self->type = RC_OPERAND_CONST;
      self->value = (unsigned)strtoul(++aux, &end, 16);

      if (end == aux) {
        return RC_INVALID_CONST_OPERAND;
      }

      aux = end;
      break;
    
    case '0':
      if (aux[1] == 'x' || aux[1] == 'X') {
        /* fall through */
    default:
        ret = rc_parse_operand_memory(self, &aux);

        if (ret < 0) {
          return ret;
        }

        break;
      }

      /* fall through */
    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':
      self->type = RC_OPERAND_CONST;
      self->value = (unsigned)strtoul(aux, &end, 10);

      if (end == aux) {
        return RC_INVALID_CONST_OPERAND;
      }

      aux = end;
      break;
    
    case '@':
      ret = rc_parse_operand_lua(self, &aux, L, funcs_ndx);

      if (ret < 0) {
        return ret;
      }

      break;
  }

  *memaddr = aux;
  return RC_OK;
}

static int rc_parse_operand_term(rc_operand_t* self, const char** memaddr, lua_State* L, int funcs_ndx) {
  const char* aux = *memaddr;
  char* end;
  int ret;

  switch (*aux) {
    case 'h': case 'H':
      self->type = RC_OPERAND_CONST;
      self->value = (unsigned)strtoul(++aux, &end, 16);

      if (end == aux) {
        return RC_INVALID_CONST_OPERAND;
      }

      aux = end;
      break;
    
    case 'v': case 'V':
      self->type = RC_OPERAND_CONST;
      self->value = (unsigned)strtoul(++aux, &end, 10);

      if (end == aux) {
        return RC_INVALID_CONST_OPERAND;
      }

      aux = end;
      break;
    
    case '0':
      if (aux[1] == 'x' || aux[1] == 'X') {
        /* fall through */
    default:
        ret = rc_parse_operand_memory(self, &aux);

        if (ret < 0) {
          return ret;
        }

        break;
      }

      /* fall through */
    case '+': case '-':
    case '1': case '2': case '3': case '4': case '5':
    case '6': case '7': case '8': case '9':
      self->type = RC_OPERAND_FP;
      self->fp_value = strtod(aux, &end);

      if (end == aux) {
        return RC_INVALID_FP_OPERAND;
      }

      aux = end;
      break;
    
    case '@':
      ret = rc_parse_operand_lua(self, &aux, L, funcs_ndx);

      if (ret < 0) {
        return ret;
      }

      break;
  }

  *memaddr = aux;
  return RC_OK;
}

int rc_parse_operand(rc_operand_t* self, const char** memaddr, int is_trigger, lua_State* L, int funcs_ndx) {
  self->size = RC_OPERAND_8_BITS;
  self->is_bcd = 0;
  self->previous = 0;

  if (is_trigger) {
    return rc_parse_operand_trigger(self, memaddr, L, funcs_ndx);
  }
  else {
    return rc_parse_operand_term(self, memaddr, L, funcs_ndx);
  }
}

unsigned rc_evaluate_operand(rc_operand_t* self, rc_peek_t peek, void* ud, lua_State* L) {
  unsigned value;

  switch (self->type) {
    case RC_OPERAND_CONST:
      value = self->value;
      break;

    case RC_OPERAND_FP:
      /* This is handled by rc_evaluate_expression. */
      return 0;
    
    case RC_OPERAND_LUA:
      lua_rawgeti(L, LUA_REGISTRYINDEX, self->function_ref);
      
      if (lua_pcall(L, 0, 1, 0) == LUA_OK) {
        if (lua_isboolean(L, -1)) {
          value = lua_toboolean(L, -1);
        }
        else {
          value = (unsigned)lua_tonumber(L, -1);
        }
      }
      else {
        value = 0;
      }

      lua_pop(L, 1);
      break;

    case RC_OPERAND_ADDRESS:
    case RC_OPERAND_DELTA:
      switch (self->size) {
        case RC_OPERAND_BIT_0:
          value = (peek(self->value, 1, ud) >> 0) & 1;
          break;

        case RC_OPERAND_BIT_1:
          value = (peek(self->value, 1, ud) >> 1) & 1;
          break;
        
        case RC_OPERAND_BIT_2:
          value = (peek(self->value, 1, ud) >> 2) & 1;
          break;
        
        case RC_OPERAND_BIT_3:
          value = (peek(self->value, 1, ud) >> 3) & 1;
          break;
        
        case RC_OPERAND_BIT_4:
          value = (peek(self->value, 1, ud) >> 4) & 1;
          break;
        
        case RC_OPERAND_BIT_5:
          value = (peek(self->value, 1, ud) >> 5 ) & 1;
          break;
        
        case RC_OPERAND_BIT_6:
          value = (peek(self->value, 1, ud) >> 6) & 1;
          break;
        
        case RC_OPERAND_BIT_7:
          value = (peek(self->value, 1, ud) >> 7) & 1;
          break;

        case RC_OPERAND_LOW:
          value = peek(self->value, 1, ud) & 0x0f;
          break;
                
        case RC_OPERAND_HIGH:
          value = (peek(self->value, 1, ud) >> 4) & 0x0f;
          break;
        
        case RC_OPERAND_8_BITS:
          value = peek(self->value, 1, ud);

          if (self->is_bcd) {
            value = ((value >> 4) & 0x0f) * 10 + (value & 0x0f);
          }

          break;

        case RC_OPERAND_16_BITS:
          value = peek(self->value, 2, ud);

          if (self->is_bcd) {
            value = ((value >> 12) & 0x0f) * 1000
                  + ((value >>  8) & 0x0f) *  100
                  + ((value >>  4) & 0x0f) *   10
                  + ((value >>  0) & 0x0f) *    1;
          }

          break;

        case RC_OPERAND_32_BITS:
          value = peek(self->value, 4, ud);

          if (self->is_bcd) {
            value = ((value >> 28) & 0x0f) * 10000000
                  + ((value >> 24) & 0x0f) *  1000000
                  + ((value >> 20) & 0x0f) *   100000
                  + ((value >> 16) & 0x0f) *    10000
                  + ((value >> 12) & 0x0f) *     1000
                  + ((value >>  8) & 0x0f) *      100
                  + ((value >>  4) & 0x0f) *       10
                  + ((value >>  0) & 0x0f) *        1;
          }

          break;
      }

      if (self->type == RC_OPERAND_DELTA) {
        unsigned previous = self->previous;
        self->previous = value;
        value = previous;
      }

      break;
  }

  return value;
}