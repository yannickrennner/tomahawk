/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2011, Leo Franchi <lfranchi@kde.org>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "LatchManager.h"

#include "audio/audioengine.h"
#include "database/database.h"

#include <QtCore/QStateMachine>
#include <QtCore/QState>
#include "sourcelist.h"
#include "database/databasecommand_socialaction.h"
#include "sourceplaylistinterface.h"

using namespace Tomahawk;

LatchManager::LatchManager( QObject* parent )
    : QObject( parent )
    , m_state( NotLatched )
{

    connect( AudioEngine::instance(), SIGNAL( playlistChanged( Tomahawk::PlaylistInterface* ) ), this, SLOT( playlistChanged( Tomahawk::PlaylistInterface* ) ) );
    connect( AudioEngine::instance(), SIGNAL( stopped() ), this, SLOT( playlistChanged() ) );
}

LatchManager::~LatchManager()
{

}

bool
LatchManager::isLatched( const source_ptr& src )
{
    return m_state == Latched && m_latchedOnTo == src;
}


void
LatchManager::latchRequest( const source_ptr& source )
{
    qDebug() << Q_FUNC_INFO;
    if ( isLatched( source ) )
        return;

    m_state = Latching;
    m_waitingForLatch = source;
    AudioEngine::instance()->playItem( source->getPlaylistInterface().data(), source->getPlaylistInterface()->nextItem() );
}

void
LatchManager::playlistChanged( PlaylistInterface* )
{
    // If we were latched on and changed, send the listening along stop
    if ( m_latchedOnTo.isNull() )
    {
        if ( m_waitingForLatch.isNull() )
            return; // Neither latched on nor waiting to be latched on, no-op

        m_latchedOnTo = m_waitingForLatch;
        m_latchedInterface = m_waitingForLatch->getPlaylistInterface();
        m_waitingForLatch.clear();
        m_state = Latched;

        DatabaseCommand_SocialAction* cmd = new DatabaseCommand_SocialAction();
        cmd->setSource( SourceList::instance()->getLocal() );
        cmd->setAction( "latchOn");
        cmd->setComment( m_latchedOnTo->userName() );
        cmd->setTimestamp( QDateTime::currentDateTime().toTime_t() );
        Database::instance()->enqueue( QSharedPointer< DatabaseCommand >( cmd ) );

        // If not, then keep waiting
        return;
    }

    // We're current latched, and the user changed playlist, so stop
    const PlaylistInterface* pi = AudioEngine::instance()->playlist();
    bool listeningAlong = false;
    source_ptr newSource;

    if ( pi && dynamic_cast< const SourcePlaylistInterface* >( pi ) )
    {
        // Check if we're listening along to someone, to make sure it's not the same person
        const SourcePlaylistInterface* sourcepi = dynamic_cast< const SourcePlaylistInterface* >( pi );
        if ( !AudioEngine::instance()->state() == AudioEngine::Stopped )
        {
            listeningAlong = true;
            newSource = sourcepi->source();
        }
    }

    SourcePlaylistInterface* origsourcepi = dynamic_cast< SourcePlaylistInterface* >( m_latchedInterface.data() );
    Q_ASSERT( origsourcepi );
    const source_ptr source = origsourcepi->source();

    // if we're currently listening along to the same source, no change
    if ( listeningAlong && ( !origsourcepi->source().isNull() && origsourcepi->source()->id() == newSource->id() ) )
        return;

    DatabaseCommand_SocialAction* cmd = new DatabaseCommand_SocialAction();
    cmd->setSource( SourceList::instance()->getLocal() );
    cmd->setAction( "latchOff");
    cmd->setComment( source->userName() );
    cmd->setTimestamp( QDateTime::currentDateTime().toTime_t() );
    Database::instance()->enqueue( QSharedPointer< DatabaseCommand >( cmd ) );

    m_latchedOnTo.clear();
    m_waitingForLatch.clear();
    m_latchedInterface.clear();

    m_state = NotLatched;
}


void
LatchManager::catchUpRequest()
{
    //it's a catch-up -- logic in audioengine should take care of it
    AudioEngine::instance()->next();
}

void
LatchManager::unlatchRequest( const source_ptr& source )
{
    AudioEngine::instance()->playItem( source->getPlaylistInterface().data(), source->getPlaylistInterface()->nextItem() );


    AudioEngine::instance()->stop();
    AudioEngine::instance()->setPlaylist( 0 );
}
