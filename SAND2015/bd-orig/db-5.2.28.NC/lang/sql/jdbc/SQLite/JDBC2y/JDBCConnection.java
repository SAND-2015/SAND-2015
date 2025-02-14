package SQLite.JDBC2y;

import java.sql.Array;
import java.sql.Blob;
import java.sql.CallableStatement;
import java.sql.Clob;
import java.sql.DatabaseMetaData;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.Savepoint;
import java.sql.Statement;
import java.sql.Struct;
import java.util.Properties;

public class JDBCConnection
    implements java.sql.Connection, SQLite.BusyHandler {

    /**
     * Open database.
     */
    protected DatabaseX db;

    /**
     * Database URL.
     */
    protected String url;

    /**
     * Character encoding.
     */
    protected String enc;

    /**
     * SQLite 3 VFS to use.
     */
    protected String vfs;

    /**
     * Autocommit flag, true means autocommit.
     */
    protected boolean autocommit = true;

    /**
     * In-transaction flag.
     * Can be true only when autocommit false.
     */
    protected boolean intrans = false;

    /**
     * Timeout for Database.exec()
     */
    protected int timeout = 1000000;

    /**
     * Use double/julian date representation.
     */
    protected boolean useJulian = false;

    /**
     * File name of database.
     */
    private String dbfile = null;

    /**
     * Reference to meta data or null.
     */
    private JDBCDatabaseMetaData meta = null;

    /**
     * Base time value for timeout handling.
     */
    private long t0;

    /**
     * Database in readonly mode.
     */
    private boolean readonly = false;

    /**
     * Transaction isolation mode.
     */
    private int trmode = TRANSACTION_SERIALIZABLE;

    private boolean busy0(DatabaseX db, int count) {
	if (count <= 1) {
	    t0 = System.currentTimeMillis();
	}
	if (db != null) {
	    long t1 = System.currentTimeMillis();
	    if (t1 - t0 > timeout) {
		return false;
	    }
	    db.wait(100);
	    return true;
	}
	return false;
    }

    public boolean busy(String table, int count) {
	return busy0(db, count);
    }

    protected boolean busy3(DatabaseX db, int count) {
	if (count <= 1) {
	    t0 = System.currentTimeMillis();
	}
	if (db != null) {
	    long t1 = System.currentTimeMillis();
	    if (t1 - t0 > timeout) {
		return false;
	    }
	    return true;
	}
	return false;
    }

    private DatabaseX open(boolean readonly) throws SQLException {
	DatabaseX dbx = null;
	try {
	    dbx = new DatabaseX();
	    dbx.open(dbfile, readonly ? SQLite.Constants.SQLITE_OPEN_READONLY :
		     (SQLite.Constants.SQLITE_OPEN_READWRITE |
		      SQLite.Constants.SQLITE_OPEN_CREATE), vfs);
	    dbx.set_encoding(enc);
	} catch (SQLite.Exception e) {
	    throw new SQLException(e.toString());
	}
	int loop = 0;
	while (true) {
	    try {
		dbx.exec("PRAGMA short_column_names = off;", null);
		dbx.exec("PRAGMA full_column_names = on;", null);
		dbx.exec("PRAGMA empty_result_callbacks = on;", null);
		if (SQLite.Database.version().compareTo("2.6.0") >= 0) {
		    dbx.exec("PRAGMA show_datatypes = on;", null);
		}
	    } catch (SQLite.Exception e) {
		if (dbx.last_error() != SQLite.Constants.SQLITE_BUSY ||
		    !busy0(dbx, ++loop)) {
		    try {
			dbx.close();
		    } catch (SQLite.Exception ee) {
		    }
		    throw new SQLException(e.toString());
		}
		continue;
	    }
	    break;
	}
	return dbx;
    }

    public JDBCConnection(String url, String enc, String pwd, String drep,
			  String vfs)
	throws SQLException {
	if (url.startsWith("sqlite:/")) {
	    dbfile = url.substring(8);
	} else if (url.startsWith("jdbc:sqlite:/")) {
	    dbfile = url.substring(13);
	} else {
	    throw new SQLException("unsupported url");
	}
	this.url = url;
	this.enc = enc;
	this.vfs = vfs;
	try {
	    db = open(readonly);
	    try {
		if (pwd != null && pwd.length() > 0) {
		    db.key(pwd);
		}
	    } catch (SQLite.Exception se) {
		throw new
		    SQLException("error while setting key: " + se.toString());
	    }
	    db.busy_handler(this);
	} catch (SQLException e) {
	    if (db != null) {
		try {
		    db.close();
		} catch (SQLite.Exception ee) {
		}
	    }
	    throw e;
	}
	useJulian = drep != null &&
	    (drep.startsWith("j") || drep.startsWith("J"));
    }

    /* non-standard */
    public SQLite.Database getSQLiteDatabase() {
	return db;
    }
  
    public Statement createStatement() {
	JDBCStatement s = new JDBCStatement(this);
	return s;
    }

    public Statement createStatement(int resultSetType,
				     int resultSetConcurrency)
	throws SQLException {
	if (resultSetType != ResultSet.TYPE_FORWARD_ONLY &&
	    resultSetType != ResultSet.TYPE_SCROLL_INSENSITIVE &&
	    resultSetType != ResultSet.TYPE_SCROLL_SENSITIVE) {
	    throw new SQLException("unsupported result set type");
	}
	if (resultSetConcurrency != ResultSet.CONCUR_READ_ONLY &&
	    resultSetConcurrency != ResultSet.CONCUR_UPDATABLE) {
	    throw new SQLException("unsupported result set concurrency");
	}
	JDBCStatement s = new JDBCStatement(this);
	return s;
    }
	
    public DatabaseMetaData getMetaData() throws SQLException {
	if (meta == null) {
	    meta = new JDBCDatabaseMetaData(this);
	}
	return meta;
    }

    public void close() throws SQLException {
	try {
	    rollback();
	} catch (SQLException e) {
	    /* ignored */
	}
	intrans = false;
	if (db != null) {
	    try {
		db.close();
		db = null;
	    } catch (SQLite.Exception e) {
		throw new SQLException(e.toString());
	    }
	}
    }

    public boolean isClosed() throws SQLException {
	return db == null;
    }

    public boolean isReadOnly() throws SQLException {
	return readonly;
    }

    public void clearWarnings() throws SQLException {
    }

    public void commit() throws SQLException {
	if (db == null) {
	    throw new SQLException("stale connection");
	}
	if (!intrans) {
	    return;
	}
	try {
	    db.exec("COMMIT", null);
	    intrans = false;
	} catch (SQLite.Exception e) {
	    throw new SQLException(e.toString());
	}
    }

    public boolean getAutoCommit() throws SQLException {
	return autocommit;
    }

    public String getCatalog() throws SQLException {
	return null;
    }

    public int getTransactionIsolation() throws SQLException {
	return trmode;
    }

    public SQLWarning getWarnings() throws SQLException {
	return null;
    }

    public String nativeSQL(String sql) throws SQLException {
	throw new SQLException("not supported");
    }

    public CallableStatement prepareCall(String sql) throws SQLException {
	throw new SQLException("not supported");
    }

    public CallableStatement prepareCall(String sql, int x, int y)
	throws SQLException {
	throw new SQLException("not supported");
    }

    public PreparedStatement prepareStatement(String sql) throws SQLException {
	JDBCPreparedStatement s = new JDBCPreparedStatement(this, sql);
	return s;
    }

    public PreparedStatement prepareStatement(String sql, int resultSetType,
					      int resultSetConcurrency)
	throws SQLException {
	if (resultSetType != ResultSet.TYPE_FORWARD_ONLY &&
	    resultSetType != ResultSet.TYPE_SCROLL_INSENSITIVE &&
	    resultSetType != ResultSet.TYPE_SCROLL_SENSITIVE) {
	    throw new SQLException("unsupported result set type");
	}
	if (resultSetConcurrency != ResultSet.CONCUR_READ_ONLY &&
	    resultSetConcurrency != ResultSet.CONCUR_UPDATABLE) {
	    throw new SQLException("unsupported result set concurrency");
	}
	JDBCPreparedStatement s = new JDBCPreparedStatement(this, sql);
	return s;
    }

    public void rollback() throws SQLException {
	if (db == null) {
	    throw new SQLException("stale connection");
	}
	if (!intrans) {
	    return;
	}
	try {
	    db.exec("ROLLBACK", null);
	    intrans = false;
	} catch (SQLite.Exception e) {
	    throw new SQLException(e.toString());
	}
    }

    public void setAutoCommit(boolean ac) throws SQLException {
	if (ac && intrans && db != null) {
	    try {
		db.exec("ROLLBACK", null);
	    } catch (SQLite.Exception e) {
		throw new SQLException(e.toString());
	    } finally {
		intrans = false;
	    }
	}
	autocommit = ac;
    }

    public void setCatalog(String catalog) throws SQLException {
    }

    public void setReadOnly(boolean ro) throws SQLException {
	if (intrans) {
	    throw new SQLException("incomplete transaction");
	}
	if (ro != readonly) {
	    DatabaseX dbx = null;
	    try {
		dbx = open(ro);
		db.close();
		db = dbx;
		dbx = null;
		readonly = ro;
	    } catch (SQLException e) {
		throw e;
	    } catch (SQLite.Exception ee) {
		if (dbx != null) {
		    try {
			dbx.close();
		    } catch (SQLite.Exception eee) {
		    }
		}
		throw new SQLException(ee.toString());
	    }
	}
    }

    public void setTransactionIsolation(int level) throws SQLException {
	if (db.is3() && SQLite.JDBCDriver.sharedCache) {
	    String flag = null;
	    if (level == TRANSACTION_READ_UNCOMMITTED &&
		trmode != TRANSACTION_READ_UNCOMMITTED) {
		flag = "on";
	    } else if (level == TRANSACTION_SERIALIZABLE &&
		       trmode != TRANSACTION_SERIALIZABLE) {
		flag = "off";
	    }
	    if (flag != null) {
		try {
		    db.exec("PRAGMA read_uncommitted = " + flag + ";", null);
		    trmode = level;
		} catch (java.lang.Exception e) {
		}
	    }
	}
	if (level != trmode) {
	    throw new SQLException("not supported");
	}
    }

    public java.util.Map<String, Class<?>> getTypeMap() throws SQLException {
	throw new SQLException("not supported");
    }

    public void setTypeMap(java.util.Map map) throws SQLException {
	throw new SQLException("not supported");
    }
  
    public int getHoldability() throws SQLException {
	return ResultSet.HOLD_CURSORS_OVER_COMMIT;
    }

    public void setHoldability(int holdability) throws SQLException {
	if (holdability == ResultSet.HOLD_CURSORS_OVER_COMMIT) {
	    return;
	}
	throw new SQLException("not supported");
    }

    public Savepoint setSavepoint() throws SQLException {
	throw new SQLException("not supported");
    }

    public Savepoint setSavepoint(String name) throws SQLException {
	throw new SQLException("not supported");
    }

    public void rollback(Savepoint x) throws SQLException {
	throw new SQLException("not supported");
    }

    public void releaseSavepoint(Savepoint x) throws SQLException {
	throw new SQLException("not supported");
    }

    public Statement createStatement(int resultSetType,
				     int resultSetConcurrency,
				     int resultSetHoldability)
	throws SQLException {
	if (resultSetHoldability != ResultSet.HOLD_CURSORS_OVER_COMMIT) {
	    throw new SQLException("not supported");
	}
	return createStatement(resultSetType, resultSetConcurrency);
    }

    public PreparedStatement prepareStatement(String sql, int resultSetType,
					      int resultSetConcurrency,
					      int resultSetHoldability)
	throws SQLException {
	if (resultSetHoldability != ResultSet.HOLD_CURSORS_OVER_COMMIT) {
	    throw new SQLException("not supported");
	}
	return prepareStatement(sql, resultSetType, resultSetConcurrency);
    }

    public CallableStatement prepareCall(String sql, int x, int y, int z)
	throws SQLException {
	throw new SQLException("not supported");
    }

    public PreparedStatement prepareStatement(String sql, int autokeys)
	throws SQLException {
	if (autokeys != Statement.NO_GENERATED_KEYS) {
	    throw new SQLException("not supported");
	}
	return prepareStatement(sql);
    }

    public PreparedStatement prepareStatement(String sql, int colIndexes[])
	throws SQLException {
	throw new SQLException("not supported");
    }

    public PreparedStatement prepareStatement(String sql, String columns[])
	throws SQLException {
	throw new SQLException("not supported");
    }

}

class DatabaseX extends SQLite.Database {

    static Object lock = new Object();

    public DatabaseX() {
	super();
    }

    void wait(int ms) {
	try {
	    synchronized (lock) {
		lock.wait(ms);
	    }
	} catch (java.lang.Exception e) {
	}
    }

    public void exec(String sql, SQLite.Callback cb)
	throws SQLite.Exception {
	super.exec(sql, cb);
	synchronized (lock) {
	    lock.notifyAll();
	}
    }

    public void exec(String sql, SQLite.Callback cb, String args[])
	throws SQLite.Exception {
	super.exec(sql, cb, args);
	synchronized (lock) {
	    lock.notifyAll();
	}
    }

    public SQLite.TableResult get_table(String sql, String args[])
	throws SQLite.Exception {
	SQLite.TableResult ret = super.get_table(sql, args);
	synchronized (lock) {
	    lock.notifyAll();
	}
	return ret;
    }

    public void get_table(String sql, String args[], SQLite.TableResult tbl)
	throws SQLite.Exception {
	super.get_table(sql, args, tbl);
	synchronized (lock) {
	    lock.notifyAll();
	}
    }

}
