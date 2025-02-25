#ifndef SESSION_TRACKER_INCLUDED
#define SESSION_TRACKER_INCLUDED

/* Copyright (c) 2014, 2022, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <stddef.h>
#include <sys/types.h>

#include <map>

#include "lex_string.h"
#include "sql/rpl_context.h"
#include "thr_lock.h"  // thr_lock_type

class String;
class THD;
class set_var;

struct CHARSET_INFO;

enum enum_session_tracker {
  SESSION_SYSVARS_TRACKER, /* Session system variables */
  CURRENT_SCHEMA_TRACKER,  /* Current schema */
  SESSION_STATE_CHANGE_TRACKER,
  SESSION_GTIDS_TRACKER,    /* Tracks GTIDs */
  TRANSACTION_INFO_TRACKER, /* Transaction state */
  /*
    There should be a one-to-one mapping between this enum members and the
    members from enum enum_session_state_type defined in mysql_com.h.
    TRANSACTION_INFO_TRACKER maps to 2 types of tracker which are:
    SESSION_TRACK_TRANSACTION_CHARACTERISTICS, SESSION_TRACK_TRANSACTION_STATE.
    Thus introduced a dummy tracker type on server side to keep trackers in sync
    between client and server.
  */
  TRACK_TRANSACTION_STATE,
  SESSION_RESP_ATTR_TRACKER,
};

#define SESSION_TRACKER_END SESSION_RESP_ATTR_TRACKER

#define TX_TRACKER_GET(a)                                            \
  Transaction_state_tracker *a =                                     \
      (Transaction_state_tracker *)thd->session_tracker.get_tracker( \
          TRANSACTION_INFO_TRACKER)

/**
  State_tracker
  -------------
  An abstract class that defines the interface for any of the server's
  'session state change tracker'. A tracker, however, is a sub- class of
  this class which takes care of tracking the change in value of a part-
  icular session state type and thus defines various methods listed in this
  interface. The change information is later serialized and transmitted to
  the client through protocol's OK packet.

  Tracker system variables :-
  A tracker is normally mapped to a system variable. So in order to enable,
  disable or modify the sub-entities of a tracker, the user needs to modify
  the respective system variable either through SET command or via command
  line option. As required in system variable handling, this interface also
  includes two functions to help in the verification of the supplied value
  (ON_CHECK) and the updation (ON_UPDATE) of the tracker system variable,
  namely - check() and update().
*/

class State_tracker {
 protected:
  /** Is tracking enabled for a particular session state type ? */
  bool m_enabled;

  /** Has the session state type changed ? */
  bool m_changed;

 public:
  /** Constructor */
  State_tracker() : m_enabled(false), m_changed(false) {}

  /** Destructor */
  virtual ~State_tracker() = default;

  /** Getters */
  bool is_enabled() const { return m_enabled; }

  bool is_changed() const { return m_changed; }

  /** Called in the constructor of THD*/
  virtual bool enable(THD *thd) = 0;

  /** To be invoked when the tracker's system variable is checked (ON_CHECK). */
  virtual bool check(THD *thd, set_var *var) = 0;

  /** To be invoked when the tracker's system variable is updated (ON_UPDATE).*/
  virtual bool update(THD *thd) = 0;

  /** Store changed data into the given buffer. */
  virtual bool store(THD *thd, String &buf) = 0;

  /** Reset any internal state at the end of statement. */
  virtual void reset() = 0;

  /** Mark the entity as changed. */
  virtual void mark_as_changed(THD *thd, LEX_CSTRING name,
                               const LEX_CSTRING *value = nullptr) = 0;

  virtual void claim_memory_ownership(bool claim [[maybe_unused]]) {}
};

/**
  Session_tracker
  ---------------
  This class holds an object each for all tracker classes and provides
  methods necessary for systematic detection and generation of session
  state change information.
*/

class Session_tracker {
 private:
  State_tracker *m_trackers[SESSION_TRACKER_END + 1];

 public:
  Session_tracker(Session_tracker const &) = delete;

  Session_tracker &operator=(Session_tracker const &) = delete;

  /** Constructor */
  Session_tracker() = default;

  /** Destructor */
  ~Session_tracker() = default;
  /**
    Initialize Session_tracker objects and enable them based on the
    tracker_xxx variables' value that the session inherit from global
    variables at the time of session initialization (see plugin_thdvar_init).
  */
  void init(const CHARSET_INFO *char_set);
  void enable(THD *thd);
  bool server_boot_verify(const CHARSET_INFO *char_set, LEX_STRING var_list);

  /** Returns the pointer to the tracker object for the specified tracker. */
  State_tracker *get_tracker(enum_session_tracker tracker) const;

  /** Checks if m_enabled flag is set for any of the tracker objects. */
  bool enabled_any();

  /** Checks if m_changed flag is set for any of the tracker objects. */
  bool changed_any();

  /**
    Stores the session state change information of all changes session state
    type entities into the specified buffer.
  */
  void store(THD *thd, String &main_buf);
  void deinit() {
    for (int i = 0; i <= SESSION_TRACKER_END; i++) delete m_trackers[i];
  }

  void reset() {
    for (int i = 0; i <= SESSION_TRACKER_END; i++) {
      m_trackers[i]->reset();
    }
  }

  void claim_memory_ownership(bool claim);
};

/*
  Session_state_change_tracker
  ----------------------------
  This is a boolean tracker class that will monitor any change that contributes
  to a session state change.
  Attributes that contribute to session state change include:
     - Successful change to System variables
     - User defined variables assignments
     - temporary tables created, altered or deleted
     - prepared statements added or removed
     - change in current database
*/

class Session_state_change_tracker : public State_tracker {
 public:
  Session_state_change_tracker();
  bool enable(THD *thd) override;
  bool check(THD *, set_var *) override { return false; }
  bool update(THD *thd) override;
  bool store(THD *, String &buf) override;
  void mark_as_changed(
      THD *thd, LEX_CSTRING tracked_item_name,
      const LEX_CSTRING *tracked_item_value = nullptr) override;
  bool is_state_changed();
  void reset() override;
};

/**
  Transaction_state_tracker
  ----------------------
  This is a tracker class that enables & manages the tracking of
  current transaction info for a particular connection.
*/

/**
  Transaction state (no transaction, transaction active, work attached, etc.)
*/
enum enum_tx_state {
  TX_EMPTY = 0,            ///< "none of the below"
  TX_EXPLICIT = 1,         ///< an explicit transaction is active
  TX_IMPLICIT = 2,         ///< an implicit transaction is active
  TX_READ_TRX = 4,         ///<     transactional reads  were done
  TX_READ_UNSAFE = 8,      ///< non-transaction   reads  were done
  TX_WRITE_TRX = 16,       ///<     transactional writes were done
  TX_WRITE_UNSAFE = 32,    ///< non-transactional writes were done
  TX_STMT_UNSAFE = 64,     ///< "unsafe" (non-deterministic like UUID()) stmts
  TX_RESULT_SET = 128,     ///< result-set was sent
  TX_WITH_SNAPSHOT = 256,  ///< WITH CONSISTENT SNAPSHOT was used
  TX_LOCKED_TABLES = 512,  ///< LOCK TABLES is active
  TX_STMT_DML = 1024       ///< a DML statement (known before data is accessed)
};

/**
  Transaction access mode
*/
enum enum_tx_read_flags {
  TX_READ_INHERIT = 0,  ///< not explicitly set, inherit session.tx_read_only
  TX_READ_ONLY = 1,     ///< START TRANSACTION READ ONLY,  or tx_read_only=1
  TX_READ_WRITE = 2,    ///< START TRANSACTION READ WRITE, or tx_read_only=0
};

/**
  Transaction isolation level
*/
enum enum_tx_isol_level {
  TX_ISOL_INHERIT = 0,  ///< not explicitly set, inherit session.tx_isolation
  TX_ISOL_UNCOMMITTED = 1,
  TX_ISOL_COMMITTED = 2,
  TX_ISOL_REPEATABLE = 3,
  TX_ISOL_SERIALIZABLE = 4
};

/**
  Transaction tracking level
*/
enum enum_session_track_transaction_info {
  TX_TRACK_NONE = 0,     ///< do not send tracker items on transaction info
  TX_TRACK_STATE = 1,    ///< track transaction status
  TX_TRACK_CHISTICS = 2  ///< track status and characteristics
};

class Transaction_state_tracker : public State_tracker {
 public:
  /** Constructor */
  Transaction_state_tracker();
  bool enable(THD *thd) override { return update(thd); }
  bool check(THD *, set_var *) override { return false; }
  bool update(THD *thd) override;
  bool store(THD *thd, String &buf) override;
  void mark_as_changed(
      THD *thd, LEX_CSTRING tracked_item_name,
      const LEX_CSTRING *tracked_item_value = nullptr) override;

  /** Change transaction characteristics */
  void set_read_flags(THD *thd, enum enum_tx_read_flags flags);
  void set_isol_level(THD *thd, enum enum_tx_isol_level level);

  /** Change transaction state */
  void clear_trx_state(THD *thd, uint clear);
  void add_trx_state(THD *thd, uint add);
  void add_trx_state_from_thd(THD *thd);
  void end_trx(THD *thd);

  /** Helper function: turn table info into table access flag */
  enum_tx_state calc_trx_state(thr_lock_type l, bool has_trx);

  /** Get (possibly still incomplete) state */
  uint get_trx_state() const { return tx_curr_state; }

  void reset() override;

 private:
  enum enum_tx_changed {
    TX_CHG_NONE = 0,     ///< no changes from previous stmt
    TX_CHG_STATE = 1,    ///< state has changed from previous stmt
    TX_CHG_CHISTICS = 2  ///< characteristics have changed from previous stmt
  };

  /** any trackable changes caused by this statement? */
  uint tx_changed;

  /** transaction state */
  uint tx_curr_state, tx_reported_state;

  /** r/w or r/o set? session default? */
  enum enum_tx_read_flags tx_read_flags;

  /**  isolation level */
  enum enum_tx_isol_level tx_isol_level;

  inline void update_change_flags(THD *thd) {
    tx_changed &= ~TX_CHG_STATE;
    // Flag state changes other than "is DML"
    tx_changed |=
        ((tx_curr_state & ~TX_STMT_DML) != (tx_reported_state & ~TX_STMT_DML))
            ? TX_CHG_STATE
            : 0;
    if (tx_changed != TX_CHG_NONE) mark_as_changed(thd, {});
  }
};

/*
  Session_resp_attr_tracker
  ----------------------
  This is a tracker class that will monitor response attributes
*/

class Session_resp_attr_tracker : public State_tracker {
 private:
  Session_resp_attr_tracker(const Session_resp_attr_tracker &) = delete;
  Session_resp_attr_tracker &operator=(const Session_resp_attr_tracker &) =
      delete;

  std::map<std::string, std::string> attrs_;

 public:
  Session_resp_attr_tracker() {
    m_changed = false;
    m_enabled = false;
  }
  // 65535 is the HARD LIMIT. Realistically we should need nothing
  // close to this
  static constexpr size_t MAX_RESP_ATTR_LEN = 60000;

  bool enable(THD *thd) override;
  bool check(THD *, set_var *) override { return false; }
  bool update(THD *thd) override { return enable(thd); }
  bool store(THD *thd, String &buf) override;
  void mark_as_changed(THD *thd, LEX_CSTRING key,
                       const LEX_CSTRING *value) override;
  const std::map<std::string, std::string> &attributes() { return attrs_; }
  void reset() override;
};

/**
  Session_gtids_tracker
  ---------------------------------
  This is a tracker class that enables & manages the tracking of gtids for
  relaying to the connectors the information needed to handle session
  consistency.
*/
class Session_gtids_ctx_encoder;
class Session_gtids_tracker
    : public State_tracker,
      Session_consistency_gtids_ctx::Ctx_change_listener {
 private:
  Session_gtids_ctx_encoder *m_encoder;

 public:
  /** Constructor */
  Session_gtids_tracker()
      : Session_consistency_gtids_ctx::Ctx_change_listener(),
        m_encoder(nullptr) {}

  ~Session_gtids_tracker() override;

  bool enable(THD *thd) override { return update(thd); }
  bool check(THD *, set_var *) override { return false; }
  bool update(THD *thd) override;
  bool store(THD *thd, String &buf) override;
  bool storeToStdString(THD *thd, std::string &buf);
  void mark_as_changed(
      THD *thd, LEX_CSTRING tracked_item_name,
      const LEX_CSTRING *tracked_item_value = nullptr) override;

  // implementation of the Session_gtids_ctx::Ctx_change_listener
  void notify_session_gtids_ctx_change() override {
    mark_as_changed(nullptr, {});
  }
  void reset() override;
};

/**
  Session_sysvars_tracker
  -----------------------
  This is a tracker class that enables & manages the tracking of session
  system variables. It internally maintains a hash of user supplied variable
  names and a boolean field to store if the variable was changed by the last
  statement.
*/

class Session_sysvars_tracker : public State_tracker {
 private:
  struct sysvar_node_st {
    LEX_CSTRING m_sysvar_name;
    bool m_changed;
  };

  class vars_list {
   private:
    /**
      Registered system variables. (@@session_track_system_variables)
      A hash to store the name of all the system variables specified by the
      user.
    */
    using sysvar_map =
        collation_unordered_map<std::string,
                                unique_ptr_my_free<sysvar_node_st>>;
    std::unique_ptr<sysvar_map> m_registered_sysvars;
    char *variables_list;
    /**
      The boolean which when set to true, signifies that every variable
      is to be tracked.
    */
    bool track_all;
    const CHARSET_INFO *m_char_set;

    void init(const CHARSET_INFO *char_set) {
      variables_list = nullptr;
      m_char_set = char_set;
      m_registered_sysvars.reset(
          new sysvar_map(char_set, key_memory_THD_Session_tracker));
    }

    void free_hash() { m_registered_sysvars.reset(); }

    sysvar_node_st *search(const uchar *token, size_t length) {
      return find_or_nullptr(
          *m_registered_sysvars,
          std::string(pointer_cast<const char *>(token), length));
    }

   public:
    vars_list(const CHARSET_INFO *char_set) { init(char_set); }

    void claim_memory_ownership(bool claim) { my_claim(variables_list, claim); }

    ~vars_list() {
      if (variables_list) my_free(variables_list);
      variables_list = nullptr;
    }

    sysvar_node_st *search(sysvar_node_st *node, const LEX_CSTRING &tmp) {
      sysvar_node_st *res;
      res = search((const uchar *)tmp.str, tmp.length);
      if (!res) {
        if (track_all) {
          insert(node, tmp);
          return search((const uchar *)tmp.str, tmp.length);
        }
      }
      return res;
    }

    sysvar_map::iterator begin() const { return m_registered_sysvars->begin(); }
    sysvar_map::iterator end() const { return m_registered_sysvars->end(); }

    const CHARSET_INFO *char_set() const { return m_char_set; }

    bool insert(sysvar_node_st *node, const LEX_CSTRING &var);
    void reset();
    bool update(vars_list *from, THD *thd);
    bool parse_var_list(THD *thd, LEX_STRING var_list, bool throw_error,
                        const CHARSET_INFO *char_set, bool session_created);
  };
  /**
    Two objects of vars_list type are maintained to manage
    various operations on variables_list.
  */
  vars_list *orig_list, *tool_list;

 public:
  /** Constructor */
  Session_sysvars_tracker(const CHARSET_INFO *char_set) {
    orig_list = new (std::nothrow) vars_list(char_set);
    tool_list = new (std::nothrow) vars_list(char_set);
  }

  /** Destructor */
  ~Session_sysvars_tracker() override {
    if (orig_list) delete orig_list;
    if (tool_list) delete tool_list;
  }

  void populate_changed_sysvars(THD *thd,
                                std::map<std::string, std::string> &sysvars);

  /**
    Method used to check the validity of string provided
    for session_track_system_variables during the server
    startup.
  */
  static bool server_init_check(const CHARSET_INFO *char_set,
                                LEX_STRING var_list) {
    vars_list dummy(char_set);
    bool result;
    result = dummy.parse_var_list(nullptr, var_list, false, char_set, true);
    return result;
  }

  void reset() override;
  bool enable(THD *thd) override;
  bool check(THD *thd, set_var *var) override;
  bool update(THD *thd) override;
  bool store(THD *thd, String &buf) override;
  void mark_as_changed(
      THD *thd, LEX_CSTRING tracked_item_name,
      const LEX_CSTRING *tracked_item_value = nullptr) override;
  /* callback */
  static const uchar *sysvars_get_key(const uchar *entry, size_t *length);

  void claim_memory_ownership(bool claim) override {
    if (orig_list != nullptr) orig_list->claim_memory_ownership(claim);
    if (tool_list != nullptr) tool_list->claim_memory_ownership(claim);
  }
};

#endif /* SESSION_TRACKER_INCLUDED */
