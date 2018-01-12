//******************************************************************************
//******************************************************************************

#include "xbridgetransactionsmodel.h"
#include "xbridge/xbridgetransaction.h"
#include "xbridge/xbridgeapp.h"
// #include "xbridgeconnector.h"
#include "xbridge/xuiconnector.h"
#include "xbridge/util/xutil.h"
#include "xbridge/util/xbridgeerror.h"

#include <QApplication>

#include <boost/date_time/posix_time/posix_time.hpp>

//******************************************************************************
//******************************************************************************
XBridgeTransactionsModel::XBridgeTransactionsModel()
{
    m_columns << trUtf8("TOTAL")
              << trUtf8("SIZE")
              << trUtf8("BID")
              << trUtf8("DATE")
              << trUtf8("STATE");

    xuiConnector.NotifyXBridgePendingTransactionReceived.connect
            (boost::bind(&XBridgeTransactionsModel::onTransactionReceivedExtSignal, this, _1));

    xuiConnector.NotifyXBridgeTransactionStateChanged.connect
            (boost::bind(&XBridgeTransactionsModel::onTransactionStateChangedExtSignal, this, _1, _2));
    xuiConnector.NotifyXBridgeTransactionCancelled.connect
            (boost::bind(&XBridgeTransactionsModel::onTransactionCancelledExtSignal, this, _1, _2, _3));

    connect(&m_timer, SIGNAL(timeout()), this, SLOT(onTimer()));
    m_timer.start(3000);
}

//******************************************************************************
//******************************************************************************
XBridgeTransactionsModel::~XBridgeTransactionsModel()
{
    xuiConnector.NotifyXBridgePendingTransactionReceived.disconnect
            (boost::bind(&XBridgeTransactionsModel::onTransactionReceivedExtSignal, this, _1));

    xuiConnector.NotifyXBridgeTransactionStateChanged.disconnect
            (boost::bind(&XBridgeTransactionsModel::onTransactionStateChangedExtSignal, this, _1, _2));

    xuiConnector.NotifyXBridgeTransactionCancelled.disconnect
            (boost::bind(&XBridgeTransactionsModel::onTransactionCancelledExtSignal, this, _1, _2, _3));
}

//******************************************************************************
//******************************************************************************
int XBridgeTransactionsModel::rowCount(const QModelIndex &) const
{
    return m_transactions.size();
}

//******************************************************************************
//******************************************************************************
int XBridgeTransactionsModel::columnCount(const QModelIndex &) const
{
    return m_columns.size();
}

//******************************************************************************
//******************************************************************************
QVariant XBridgeTransactionsModel::data(const QModelIndex & idx, int role) const
{

    if (!idx.isValid())
    {
        return QVariant();
    }

    if (idx.row() < 0 || idx.row() >= static_cast<int>(m_transactions.size()))
    {
        return QVariant();
    }

    const XBridgeTransactionDescr & d = m_transactions[idx.row()];

    if (role == Qt::DisplayRole)
    {
        switch (idx.column())
        {
            case Total:
            {
                double amount = (double)d.fromAmount / XBridgeTransactionDescr::COIN;
                QString text = QString("%1 %2").arg(QString::number(amount, 'f', 12).remove(QRegExp("\\.?0+$"))).arg(QString::fromStdString(d.fromCurrency));

                return QVariant(text);
            }
            case Size:
            {
                double amount = (double)d.toAmount / XBridgeTransactionDescr::COIN;
                QString text = QString("%1 %2").arg(QString::number(amount, 'f', 12).remove(QRegExp("\\.?0+$"))).arg(QString::fromStdString(d.toCurrency));

                return QVariant(text);
            }
            case BID:
            {
                double amountTotal = (double)d.fromAmount / XBridgeTransactionDescr::COIN;
                double amountSize = (double)d.toAmount / XBridgeTransactionDescr::COIN;
                double bid = amountTotal / amountSize;
                QString text = QString::number(bid, 'f', 12).remove(QRegExp("\\.?0+$"));

                return QVariant(text);
            }
            case Date:
            {
                return QVariant(QString::fromUtf8(boost::posix_time::to_simple_string(d.created).c_str()));
            }
            case State:
            {
                return QVariant(transactionState(d.state));
            }

            default:
                return QVariant();
        }
    }

    if(role == rawStateRole)
    {
        if(idx.column() == State)
            return QVariant(d.state);
        else
            return QVariant();
    }

    return QVariant();
}

//******************************************************************************
//******************************************************************************
QVariant XBridgeTransactionsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal)
    {
        if (role == Qt::DisplayRole)
        {
            return m_columns[section];
        }
    }
    return QVariant();
}

//******************************************************************************
//******************************************************************************
XBridgeTransactionDescr XBridgeTransactionsModel::item(const unsigned int index) const
{
    if (index >= m_transactions.size())
    {
        XBridgeTransactionDescr dummy;
        dummy.state = XBridgeTransactionDescr::trInvalid;
        return dummy;
    }

    return m_transactions[index];
}

//******************************************************************************
//******************************************************************************
bool XBridgeTransactionsModel::isMyTransaction(const unsigned int index) const
{
    if (index >= m_transactions.size())
    {
        return false;
    }

    return m_transactions[index].from.size();
}

//******************************************************************************
//******************************************************************************
xbridge::Error XBridgeTransactionsModel::newTransaction(const std::string & from,
                                              const std::string & to,
                                              const std::string & fromCurrency,
                                              const std::string & toCurrency,
                                              const double fromAmount,
                                              const double toAmount)
{
    XBridgeApp & app = XBridgeApp::instance();
    XBridgeSessionPtr ptr = app.sessionByCurrency(fromCurrency);
    if (ptr && ptr->minAmount() > fromAmount)
    {
        return xbridge::INVALID_AMOUNT;
    }

    // TODO check amount
    uint256 id = uint256();
     const auto code = XBridgeApp::instance().sendXBridgeTransaction
            (from, fromCurrency, (uint64_t)(fromAmount * XBridgeTransactionDescr::COIN),
             to,   toCurrency,   (uint64_t)(toAmount * XBridgeTransactionDescr::COIN),id);

    if (code == xbridge::SUCCESS)
    {
        XBridgeTransactionDescr d;
        d.id           = id;
        d.from         = from;
        d.to           = to;
        d.fromCurrency = fromCurrency;
        d.toCurrency   = toCurrency;
        d.fromAmount   = (boost::uint64_t)(fromAmount * XBridgeTransactionDescr::COIN);
        d.toAmount     = (boost::uint64_t)(toAmount * XBridgeTransactionDescr::COIN);
        d.txtime       = boost::posix_time::second_clock::universal_time();
        onTransactionReceived(d);
    }
    return code;
}

//******************************************************************************
//******************************************************************************
xbridge::Error XBridgeTransactionsModel::newTransactionFromPending(const uint256 &id,
                                                         const std::vector<unsigned char> &hub,
                                                         const std::string &from,
                                                         const std::string &to)
{
    unsigned int i = 0;
    for (; i < m_transactions.size(); ++i)
    {
        if (m_transactions[i].id == id && m_transactions[i].hubAddress == hub)
        {
            // found
            XBridgeTransactionDescr &d = m_transactions[i];
            d.from  = from;
            d.to    = to;
            d.state = XBridgeTransactionDescr::trAccepting;
            std::swap(d.fromCurrency, d.toCurrency);
            std::swap(d.fromAmount, d.toAmount);

            emit dataChanged(index(i, FirstColumn), index(i, LastColumn));

            // send tx
            const auto error = XBridgeApp::instance().acceptXBridgeTransaction(d.id, from, to, d.id);
            if(error != xbridge::SUCCESS)
            {
                return error;
            }

            d.txtime = boost::posix_time::second_clock::universal_time();

            break;
        }
    }

    if (i == m_transactions.size())
    {
        // not found...assert ?
        return xbridge::UNKNOWN_ERROR;
    }

    // remove all other tx with this id
    for (unsigned int i = 0; i < m_transactions.size(); ++i)
    {
        if (m_transactions[i].id == id && m_transactions[i].hubAddress != hub)
        {
            emit beginRemoveRows(QModelIndex(), i, i);
            m_transactions.erase(m_transactions.begin() + i);
            emit endRemoveRows();
            --i;
        }
    }

    return xbridge::SUCCESS;
}

//******************************************************************************
//******************************************************************************
bool XBridgeTransactionsModel::cancelTransaction(const uint256 &id)
{
    return XBridgeApp::instance().cancelXBridgeTransaction(id, crUserRequest) == xbridge::SUCCESS;
}

//******************************************************************************
//******************************************************************************
bool XBridgeTransactionsModel::rollbackTransaction(const uint256 & id)
{
    return XBridgeApp::instance().rollbackXBridgeTransaction(id) == xbridge::SUCCESS;
}

//******************************************************************************
//******************************************************************************
void XBridgeTransactionsModel::onTimer()
{
    // check pending transactions
    for (unsigned int i = 0; i < m_transactions.size(); ++i)
    {
        boost::posix_time::time_duration td =
                boost::posix_time::second_clock::universal_time() -
                m_transactions[i].txtime;

        auto id = m_transactions[i].id;

        if (m_transactions[i].state == XBridgeTransactionDescr::trNew &&
                td.total_seconds() > XBridgeTransaction::TTL/60)
        {
            m_transactions[i].state = XBridgeTransactionDescr::trOffline;
            emit dataChanged(index(i, FirstColumn), index(i, LastColumn));
        }
        else if (m_transactions[i].state == XBridgeTransactionDescr::trPending &&
                td.total_seconds() > XBridgeTransaction::TTL/6)
        {
            m_transactions[i].state = XBridgeTransactionDescr::trExpired;
            emit dataChanged(index(i, FirstColumn), index(i, LastColumn));
        }
        else if ((m_transactions[i].state == XBridgeTransactionDescr::trExpired ||
                  m_transactions[i].state == XBridgeTransactionDescr::trOffline) &&
                         td.total_seconds() < XBridgeTransaction::TTL/6)
        {
            m_transactions[i].state = XBridgeTransactionDescr::trPending;
            emit dataChanged(index(i, FirstColumn), index(i, LastColumn));
        }
        else if (m_transactions[i].state == XBridgeTransactionDescr::trExpired &&
                td.total_seconds() > XBridgeTransaction::TTL)
        {
            //if waiting time-out of transaction exceeds the limit
            emit beginRemoveRows(QModelIndex(), i, i);
            m_transactions.erase(m_transactions.begin()+i);
            emit endRemoveRows();
            --i;
            {
                //to remove also from the core
                boost::mutex::scoped_lock l(XBridgeApp::m_txLocker);
                XBridgeApp::m_historicTransactions.erase(id);
            }
            continue;
        }

        if(XBridgeApp::instance().isHistoricState(m_transactions[i].state))
        {
            //add transaction into history
            auto tmp = XBridgeTransactionDescrPtr(new XBridgeTransactionDescr(m_transactions[i]));
            {
                boost::mutex::scoped_lock l(XBridgeApp::m_txLocker);
                XBridgeApp::m_historicTransactions.insert(XBridgeApp::m_historicTransactions.end(),
                                                          std::make_pair(tmp->id, tmp));
            }
            {
                boost::mutex::scoped_lock l(XBridgeApp::m_txLocker);
                XBridgeApp::m_pendingTransactions.erase(id);
            }
        }
    }
}

//******************************************************************************
//******************************************************************************

/* XBridge signals are caught and propagated as GUI-thread QT events. The events
   are caught in 'customEvent' method, are decoded there and propagated to the handlers
   defined below. */

void XBridgeTransactionsModel::onTransactionReceived(const XBridgeTransactionDescr & tx)
{

    for (unsigned int i = 0; i < m_transactions.size(); ++i)
    {
        const XBridgeTransactionDescr & descr = m_transactions.at(i);
        if (descr.id != tx.id)
        {
            continue;
        }

        if (isMyTransaction(i))
        {
            // transaction with id - is owned, not processed more
            return;
        }

        // found
        if (descr.from.size() == 0)
        {
            m_transactions[i] = tx;
        }

        else if (descr.state < tx.state)
        {
            m_transactions[i].state = tx.state;
        }

        // update timestamp
        m_transactions[i].txtime = tx.txtime;

        emit dataChanged(index(i, FirstColumn), index(i, LastColumn));
        return;
    }

    // skip tx with other currencies
    // std::string tmp = thisCurrency().toStdString();
    // if (tx.fromCurrency != tmp && tx.toCurrency != tmp)
    // {
    //     return;
    // }

    emit beginInsertRows(QModelIndex(), 0, 0);
    m_transactions.insert(m_transactions.begin(), 1, tx);
    // std::sort(m_transactions.begin(), m_transactions.end(), std::greater<XBridgeTransactionDescr>());
    emit endInsertRows();
}

//******************************************************************************
//******************************************************************************
void XBridgeTransactionsModel::onTransactionStateChanged(const uint256 & id,
                                                         const uint32_t state)
{
    for (unsigned int i = 0; i < m_transactions.size(); ++i)
    {
        if (m_transactions[i].id == id)
        {
            // found
            m_transactions[i].state = static_cast<XBridgeTransactionDescr::State>(state);
            emit dataChanged(index(i, FirstColumn), index(i, LastColumn));
        }
    }
}

//******************************************************************************
//******************************************************************************
void XBridgeTransactionsModel::onTransactionCancelled(const uint256 & id,
                                                      const uint32_t state,
                                                      const uint32_t reason)
{
    uint32_t i = 0;
    for (XBridgeTransactionDescr & tx : m_transactions)
    {
        if (tx.id == id)
        {
            // found
            tx.state = static_cast<XBridgeTransactionDescr::State>(state);
            tx.reason = reason;
            emit dataChanged(index(i, FirstColumn), index(i, LastColumn));
        }

        ++i;
    }
}

void XBridgeTransactionsModel::customEvent(QEvent * event)
{
    // When we get here, we've crossed the thread boundary and are now
    // executing in the Qt object's thread

    if(event->type() == TRANSACTION_RECEIVED_EVENT) {

        auto e = static_cast<TransactionReceivedEvent *>(event);
        onTransactionReceived(e->tx);

    } else if(event->type() == TRANSACTION_STATE_CHANGED_EVENT) {

        auto e = static_cast<TransactionStateChangedEvent *>(event);
        onTransactionStateChanged(e->id, e->state);

    } else if(event->type() == TRANSACTION_CANCELLED_EVENT) {

        auto e = static_cast<TransactionCancelledEvent *>(event);
        onTransactionCancelled(e->id, e->state, e->reason);

    }

}

/* XBridge thread signal handlers.
   These handlers propagate signals as GUI-thread QT events
   to be caught in 'customEvent' method. */

void XBridgeTransactionsModel::onTransactionReceivedExtSignal(const XBridgeTransactionDescr & tx)
{

    QApplication::postEvent(this, new TransactionReceivedEvent(tx));
}

void XBridgeTransactionsModel::onTransactionStateChangedExtSignal(const uint256 & id,
                                                               const uint32_t state)
{
    QApplication::postEvent(this, new TransactionStateChangedEvent(id, state));
}

void XBridgeTransactionsModel::onTransactionCancelledExtSignal(const uint256 & id,
                                                      const uint32_t state,
                                                      const uint32_t reason)
{
    QApplication::postEvent(this, new TransactionCancelledEvent(id, state, reason));
}

//******************************************************************************
//******************************************************************************
QString XBridgeTransactionsModel::transactionState(const XBridgeTransactionDescr::State state) const
{
    switch (state)
    {
        case XBridgeTransactionDescr::trInvalid:        return trUtf8("Invalid");
        case XBridgeTransactionDescr::trNew:            return trUtf8("New");
        case XBridgeTransactionDescr::trPending:        return trUtf8("Open");
        case XBridgeTransactionDescr::trAccepting:      return trUtf8("Accepting");
        case XBridgeTransactionDescr::trHold:           return trUtf8("Hold");
        case XBridgeTransactionDescr::trInitialized:    return trUtf8("Initialized");
        case XBridgeTransactionDescr::trCreated:        return trUtf8("Created");
        case XBridgeTransactionDescr::trSigned:         return trUtf8("Signed");
        case XBridgeTransactionDescr::trCommited:       return trUtf8("Commited");
        case XBridgeTransactionDescr::trFinished:       return trUtf8("Finished");
        case XBridgeTransactionDescr::trCancelled:      return trUtf8("Cancelled");
        case XBridgeTransactionDescr::trRollback:       return trUtf8("Rolled Back");
        case XBridgeTransactionDescr::trRollbackFailed: return trUtf8("Rollback error");
        case XBridgeTransactionDescr::trDropped:        return trUtf8("Dropped");
        case XBridgeTransactionDescr::trExpired:        return trUtf8("Expired");
        case XBridgeTransactionDescr::trOffline:        return trUtf8("Offline");
        default:                                        return trUtf8("Unknown");
    }
}
